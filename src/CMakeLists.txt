# Compile the legacy entry point and the staged V0.2 modules.
idf_component_register(
    SRCS
        "main.c"
        "app/app_control.c"
        "app/light_fsm.c"
        "app/pump_fsm.c"
        "drivers/buttons.c"
        "drivers/htu21.c"
        "drivers/soil.c"
        "drivers/ldr.c"
        "drivers/ultrasonic.c"
        "drivers/light_pwm.c"
        "drivers/pump.c"
        "services/time_service.c"
        "services/network_service.c"
        "tasks/input_task.c"
        "tasks/sensor_task.c"
        "tasks/display_task.c"
        "tasks/network_task.c"
        "tasks/time_task.c"
        "tasks/ramp_task.c"
    INCLUDE_DIRS "."
)
