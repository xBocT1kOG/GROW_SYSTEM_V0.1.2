"""Analyze every row of the immutable log and create explicitly derived outputs.

Requires matplotlib and numpy. Run from any directory; paths are repository-relative.
No raw-file writes, normalization, sorting, interpolation or failure smoothing.
"""
import argparse
from collections import Counter
import csv
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.patches import Rectangle
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
TASKS = ('control','input','sensor','display','network','time','ramp')
COUNTERS = ('sensor_cycles htu21_errors soil_errors ldr_errors ultrasonic_errors '
            'ultrasonic_timeouts blynk_poll_ok blynk_poll_errors blynk_upload_ok '
            'blynk_upload_errors wifi_reconnects app_event_queue_overflows button_events '
            'pump_commands light_commands auto_commands light_ramp_ticks light_ramp_steps '
            'time_sync_events task_deadline_misses').split()
BOOLS = ('wifi_connected time_synced htu21_valid soil_valid ldr_valid water_valid').split()
COLLECTOR = ('total_samples full_success_samples partial_samples failed_samples').split()
STATE = ('pump LIGHT_S LIGHT_A AUTO MOD temperature humidity soil_percent water_percent '
         'htu21_error soil_error ldr_error water_error').split()
REQUIRED = ['timestamp_local','timestamp_epoch','timestamp_utc_offset','request_ok',
            'state_request_ok','diagnostics_request_ok','collector_error']
REQUIRED += ['diag_'+v for v in COUNTERS+['uptime_seconds','heap_free_bytes','heap_min_free_bytes']]
REQUIRED += [f'diag_stack_{v}_bytes' for v in TASKS]
REQUIRED += ['state_'+v for v in BOOLS+STATE]+['collector_'+v for v in COLLECTOR]


def analyze(source):
    raw = source.read_bytes()
    reader = csv.DictReader(io.StringIO(raw.decode('utf-8-sig'), newline=''))
    assert reader.fieldnames and len(set(reader.fieldnames)) == len(reader.fieldnames), 'Duplicate/empty header'
    assert set(REQUIRED) <= set(reader.fieldnames), f'Missing columns: {set(REQUIRED)-set(reader.fieldnames)}'
    rows = list(reader)
    assert len(rows)>1 and all(None not in r and all(v is not None for v in r.values()) for r in rows), 'Malformed row'
    # This run has complete diagnostic/state records. Fail explicitly on missing data
    # rather than dropping rows or replacing missing counters with zero.
    numeric = [c for c in REQUIRED if c.startswith(('diag_','collector_','state_'))
               and c!='collector_error' and c not in ['state_'+v for v in BOOLS]]
    data = {}
    for c in numeric:
        vals = [Decimal(r[c]) for r in rows]
        assert all(v.is_finite() for v in vals), f'Nonfinite value in {c}'
        if c not in ('state_temperature','state_humidity'):
            assert all(v==int(v) for v in vals), f'Noninteger value in {c}'
            data[c]=[int(v) for v in vals]
        else:
            data[c]=[float(v) for v in vals]
    epoch = [Decimal(r['timestamp_epoch']) for r in rows]
    local = [datetime.strptime(r['timestamp_local']+r['timestamp_utc_offset'],'%Y-%m-%d %H:%M:%S%z') for r in rows]
    discrepancies=[abs(float(e)-d.timestamp()) for e,d in zip(epoch,local)]
    assert max(discrepancies)<1.001, 'Local timestamp / epoch mismatch'
    times=[datetime.fromtimestamp(float(e),d.tzinfo) for e,d in zip(epoch,local)]
    intervals=[float(b-a) for a,b in zip(epoch,epoch[1:])]
    assert all(v>0 for v in intervals), 'Non-increasing collector clock'
    elapsed=float(epoch[-1]-epoch[0])
    summary={'source':source.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(raw).hexdigest(),
      'rows':len(rows),'columns':len(reader.fieldnames),'start':times[0].isoformat(timespec='milliseconds'),
      'end':times[-1].isoformat(timespec='milliseconds'),'duration_seconds':elapsed,
      'duration_hms':str(timedelta(seconds=elapsed)), 'utc_offsets':sorted(set(r['timestamp_utc_offset'] for r in rows)),
      'expected_approx_samples_at_10s':round(elapsed/10)+1,
      'intervals':{'min':min(intervals),'median':statistics.median(intervals),'max':max(intervals),
                   'p99':float(np.percentile(intervals,99)),'gap_threshold_seconds':15,
                   'gaps':[{'from':times[i].isoformat(),'to':times[i+1].isoformat(),'seconds':v}
                           for i,v in enumerate(intervals) if v>15]},
      'max_local_epoch_fraction_seconds':max(discrepancies),
      'missing_cells':{c:sum(r[c]=='' for r in rows) for c in reader.fieldnames}}
    def describe(c):
        v=data[c]
        return {'start':v[0],'final':v[-1],'min':min(v),'max':max(v),'delta':v[-1]-v[0],
                'decreases':sum(b<a for a,b in zip(v,v[1:])),
                'increases':sum(b>a for a,b in zip(v,v[1:]))}
    counters={c:describe('diag_'+c) for c in COUNTERS}
    for c in COUNTERS:
        v=data['diag_'+c]
        if 'error' in c or 'timeout' in c or c in ('wifi_reconnects','app_event_queue_overflows','task_deadline_misses','time_sync_events'):
            counters[c]['events']=[{'row':i+1,'previous':times[i-1].isoformat(timespec='milliseconds'),
                'observed':times[i].isoformat(timespec='milliseconds'),'delta':v[i]-v[i-1]}
                for i in range(1,len(v)) if v[i]!=v[i-1]]
    summary['counters']=counters
    summary['uptime']=describe('diag_uptime_seconds')
    ud=np.diff(data['diag_uptime_seconds'])
    summary['uptime'].update({'delta_min':int(ud.min()),'delta_max':int(ud.max()),
        'repeated':int(np.sum(ud==0)),'resets':[i+2 for i,d in enumerate(ud) if d<0],
        'elapsed_difference_seconds':summary['uptime']['delta']-elapsed})
    flags={}
    for c in ('request_ok','state_request_ok','diagnostics_request_ok'):
        assert all(r[c] in ('0','1') for r in rows)
        flags[c]=sum(int(r[c]) for r in rows)
    classification=[int(r['state_request_ok'])+int(r['diagnostics_request_ok']) for r in rows]
    counts=Counter(classification)
    running=[0,0,0]
    for i,(r,k) in enumerate(zip(rows,classification)):
        assert int(r['request_ok'])==int(k==2)
        running[k]+=1
        assert data['collector_total_samples'][i]==i+1
        for field,value in [('full_success_samples',running[2]),('partial_samples',running[1]),('failed_samples',running[0])]:
            assert data['collector_'+field][i]==value, 'Collector tally mismatch'
    summary['collector']={'full':counts[2],'partial':counts[1],'failed':counts[0],
        'request_success_rows':flags,'nonempty_error_rows':sum(bool(r['collector_error']) for r in rows),
        'cumulative':{c:describe('collector_'+c) for c in COLLECTOR}}
    summary['flags']={}
    for c in BOOLS:
        assert all(r['state_'+c] in ('True','False') for r in rows)
        summary['flags'][c]=dict(Counter(r['state_'+c] for r in rows))
    summary['sensor_error_snapshots']={c:dict(Counter(data['state_'+c+'_error'])) for c in ('htu21','soil','ldr','water')}
    summary['stacks']={c:describe(f'diag_stack_{c}_bytes') for c in TASKS}
    summary['heap']={c:describe('diag_'+c) for c in ('heap_free_bytes','heap_min_free_bytes')}
    hours=np.array([float(e-epoch[0])/3600 for e in epoch])
    heap=np.array(data['diag_heap_free_bytes'])
    medians=[]
    for h in range(int(hours[-1])+1):
        selected=heap[(hours>=h)&(hours<h+1)]
        medians.append({'elapsed_hour':h,'rows':len(selected),'median':float(np.median(selected)),
                        'min':int(selected.min()),'max':int(selected.max())})
    summary['heap']['hourly_bins']=medians
    summary['heap']['ols_bytes_per_hour']=float(np.polyfit(hours,heap,1)[0])
    summary['heap']['first_hour_median']=float(np.median(heap[hours<1]))
    summary['heap']['last_hour_median']=float(np.median(heap[hours>hours[-1]-1]))
    low=data['diag_heap_min_free_bytes']; last_drop=max((i for i in range(1,len(low)) if low[i]<low[i-1]),default=0)
    summary['heap']['last_min_ever_drop']=times[last_drop].isoformat(timespec='milliseconds')
    summary['sensor_continuity']={'observed_delta':counters['sensor_cycles']['delta'],
        'expected_delta_from_uptime':summary['uptime']['delta']/2,
        'effective_seconds_per_cycle':summary['uptime']['delta']/counters['sensor_cycles']['delta'],
        'cycle_increment_min':int(np.diff(data['diag_sensor_cycles']).min()),
        'cycle_increment_max':int(np.diff(data['diag_sensor_cycles']).max()),
        'timeout_fraction_percent':100*counters['ultrasonic_timeouts']['delta']/counters['sensor_cycles']['delta']}
    summary['control']={}
    for c in ('pump','LIGHT_S','LIGHT_A','AUTO','MOD'):
        v=data['state_'+c]
        summary['control'][c]={'start':v[0],'final':v[-1],'min':min(v),'max':max(v),'change_count':sum(a!=b for a,b in zip(v,v[1:]))}
        if c!='LIGHT_A':
            summary['control'][c]['transitions']=[{'previous':times[i-1].isoformat(timespec='milliseconds'),
                'observed':times[i].isoformat(timespec='milliseconds'),'from':v[i-1],'to':v[i]}
                for i in range(1,len(v)) if v[i]!=v[i-1]]
    assert source.read_bytes()==raw, 'Raw file changed during analysis'
    return summary,times,data


def figures(s,times,d,out):
    out.mkdir(parents=True,exist_ok=True)
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.titlesize':12,
                        'axes.labelsize':10,'axes.spines.top':False,'axes.spines.right':False,
                        'savefig.dpi':180,'svg.fonttype':'none'})
    tz=times[0].tzinfo
    def timeaxis(ax):
        ax.set_xlim(times[0],times[-1]);ax.xaxis.set_major_locator(mdates.HourLocator(interval=3,tz=tz))
        ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M',tz=tz))
        ax.set_xlabel('06.11.2026, время коллектора (UTC+02:00)')
        ax.grid(alpha=.2)
    def save(fig,name):
        fig.tight_layout()
        fig.savefig(out/(name+'.png'),facecolor='white')
        fig.savefig(out/(name+'.svg'),facecolor='white')
        svg=out/(name+'.svg')
        svg.write_text('\n'.join(line.rstrip() for line in svg.read_text(encoding='utf-8').splitlines())+'\n',encoding='utf-8')
        plt.close(fig)
    fig,ax=plt.subplots(figsize=(9,2.25))
    ax.set_xlim(0,4);ax.set_ylim(0,2);ax.axis('off')
    blocks=[('Наблюдение',f"{int(s['duration_seconds']//3600)} ч {int(s['duration_seconds']%3600//60)} мин"),
            ('Коллектор',f"{s['collector']['full']} полных опросов"),
            ('Очередь / сроки',f"Прирост: {s['counters']['app_event_queue_overflows']['delta']} / {s['counters']['task_deadline_misses']['delta']}"),
            ('Heap','Без явного истощения'),('Стеки','Все остатки >0'),
            ('Wi-Fi / Blynk',f"Новых подключений: {s['counters']['wifi_reconnects']['delta']}"),
            ('HC-SR04',f"Тайм-аутов: {s['counters']['ultrasonic_timeouts']['delta']}"),('Граница вывода','Только данный прогон')]
    for i,(title,detail) in enumerate(blocks):
        x=i%4;y=1-i//4;warning=i==6
        ax.add_patch(Rectangle((x+.03,y+.06),.94,.88,facecolor='#FFF1E2' if warning else '#EDF4F6',edgecolor='#A45B23' if warning else '#8CA7B3',lw=.6))
        ax.text(x+.10,y+.67,title,fontsize=10.5,fontweight='bold',color='#183344')
        ax.text(x+.10,y+.29,detail,fontsize=9.6,color='#A45B23' if warning else '#207D82')
    save(fig,'health-summary')
    fig,ax=plt.subplots(figsize=(9,4.1))
    ax.plot(times,np.array(d['diag_heap_free_bytes'])/1024,color='#247C9B',lw=.65,label='Текущая свободная heap')
    ax.step(times,np.array(d['diag_heap_min_free_bytes'])/1024,where='post',color='#A45B23',lw=1.4,label='Минимум с загрузки')
    ax.set_ylim(0,240);ax.set_ylabel('Свободная память, КиБ (1024 байта)');ax.set_title(f"Память: все {s['rows']:,} диагностических снимков".replace(',', ' '))
    timeaxis(ax);ax.legend(loc='lower left',frameon=False);save(fig,'heap')
    fig,axes=plt.subplots(3,1,figsize=(9,5.4),sharex=True)
    axes[0].step(times,d['diag_wifi_reconnects'],where='post',color='#247C9B',label='Wi-Fi: значение с загрузки')
    axes[0].set_ylim(-.1,1.6);axes[0].set_yticks([0,1]);axes[0].legend(loc='upper right',frameon=False,fontsize=9)
    zero=['blynk_poll_errors','blynk_upload_errors','app_event_queue_overflows','task_deadline_misses']
    assert all(max(d['diag_'+c])==0 for c in zero), 'Revise combined zero-counter chart'
    axes[1].plot(times,d['diag_blynk_poll_errors'],color='#247C9B',label='Blynk poll/upload, очередь, сроки: все 0')
    axes[1].set_ylim(-.15,1);axes[1].set_yticks([0,1]);axes[1].legend(loc='upper right',frameon=False,fontsize=9)
    assert d['diag_ultrasonic_errors']==d['diag_ultrasonic_timeouts']
    axes[2].step(times,d['diag_ultrasonic_timeouts'],where='post',color='#A45B23',label='HC-SR04: ошибки = тайм-ауты')
    axes[2].set_ylim(-.15,3.6);axes[2].set_yticks([0,1,2,3]);axes[2].legend(loc='upper left',frameon=False,fontsize=9)
    for ax in axes: ax.set_ylabel('Счётчик');ax.grid(alpha=.2)
    axes[0].set_title('Накопительные счётчики: начальное значение и изменения')
    timeaxis(axes[2]);save(fig,'error-counters')
    fig,ax=plt.subplots(figsize=(9,3.8))
    values=[s['stacks'][c]['min'] for c in TASKS]
    ax.barh(TASKS,values,color='#247C9B',height=.58);ax.invert_yaxis()
    ax.set_xlim(0,max(values)*1.15);ax.set_xlabel('Минимальный наблюдённый свободный остаток, байты')
    ax.set_title('Запас стека семи задач');ax.grid(axis='x',alpha=.2)
    for i,v in enumerate(values):ax.text(v+35,i,str(v),va='center',fontsize=10)
    save(fig,'stack-margins')
    fig,axes=plt.subplots(2,1,figsize=(9,4.5),sharex=True,gridspec_kw={'height_ratios':[2,1]})
    axes[0].step(times,d['state_LIGHT_S'],where='post',color='#247C9B',lw=1.5)
    axes[0].set_ylim(0,100);axes[0].set_ylabel('LIGHT_S, % команды');axes[0].grid(alpha=.2)
    axes[0].set_title('Командный уровень света и состояние автоматики')
    axes[1].step(times,d['state_MOD'],where='post',color='#A45B23',lw=1.5)
    axes[1].set_yticks([0,1,2],['IDLE','DAWN','DUSK']);axes[1].set_ylim(-.3,2.4)
    timeaxis(axes[1]);save(fig,'lighting-activity')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',default='PC_collector/2026-11-06_01-30.csv')
    args=parser.parse_args()
    source=(ROOT/args.source).resolve()
    summary,times,data=analyze(source)
    figures(summary,times,data,OUT/'assets')
    (OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:summary[k] for k in ('rows','start','end','duration_seconds','intervals','uptime','sensor_continuity','heap')},ensure_ascii=False,indent=2))
