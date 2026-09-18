/* Defines */

#define WaterMeterGPIO_1 GPIO_NUM_2
#define WaterMeterGPIO_2 GPIO_NUM_15

#define WaterValveOpen_1 gpio_set_level(GPIO_NUM_27, 1);
#define WaterValveOpen_2 gpio_set_level(GPIO_NUM_26, 1);
#define WaterValveOpen_3 gpio_set_level(GPIO_NUM_25, 1);
#define WaterValveOpen_4 gpio_set_level(GPIO_NUM_33, 1);
#define WaterValveOpen_5 gpio_set_level(GPIO_NUM_32, 1);
#define WaterValveOpen_6 gpio_set_level(GPIO_NUM_35, 1);
#define WaterValveClose_1 gpio_set_level(GPIO_NUM_27, 0);
#define WaterValveClose_2 gpio_set_level(GPIO_NUM_26, 0);
#define WaterValveClose_3 gpio_set_level(GPIO_NUM_25, 0);
#define WaterValveClose_4 gpio_set_level(GPIO_NUM_33, 0);
#define WaterValveClose_5 gpio_set_level(GPIO_NUM_32, 0);
#define WaterValveClose_6 gpio_set_level(GPIO_NUM_35, 0);

/* User application entry */
void my_application(void *arg);
void my_app_init(void);