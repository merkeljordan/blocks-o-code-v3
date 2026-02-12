// libc includes
#include <time.h>
//#include <errno.h>
//#include <sys/fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// freee RTOS related includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

// esp-idf includes
#include "esp_timer.h"
#include "esp_system.h"
//#include "esp_wifi.h"
//#include "esp_event.h"
//#include "esp_sntp.h"
#include "esp_log.h"

#include "driver/gpio.h"
//#include "nvs_flash.h"

// LVGL related include
#include "lvgl.h"
#include "lvgl_helpers.h"

#include "lcd_gui.h"
#include "tft_ui.h"


#define LCD_BACKLIGHT 32
#define PUSH_BUTTON 15

#define LV_TICK_PERIOD_MS 1

#define MAX_FAILURES 		10

static const char wifi_tag[] = "[WIFI Connect]";

SemaphoreHandle_t clock_semaphore;
SemaphoreHandle_t tab_semaphore;

static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;
lv_obj_t* sys_stat_tab;

static lv_obj_t *time_label = NULL;

static void update_time_label(void)
{
    if (!time_label) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char buf[16];
    // HH:MM:SS (24-hour). If you want 12-hour, tell me.
    strftime(buf, sizeof(buf), "%I:%M %p", &timeinfo);
if (buf[0] == '0') {
    memmove(buf, buf + 1, strlen(buf));
}

    lv_label_set_text(time_label, buf);
}


static void create_demo_application(void *pvParameters)
{
    (void) pvParameters;

    lv_obj_t *scr = lv_scr_act();

    // Screen background: pink/purple
    lv_obj_set_style_local_bg_color(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF4B6D8));
    lv_obj_set_style_local_bg_opa(scr, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    // ---------- Header ----------
    lv_obj_t *header = lv_cont_create(scr, NULL);
    lv_obj_set_size(header, 320, 32);
    lv_obj_align(header, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);

    // Header background: deeper purple
    lv_obj_set_style_local_bg_color(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xC77DFF));
    lv_obj_set_style_local_bg_opa(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(header, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

    // Title (left)
    lv_obj_t *title = lv_label_create(header, NULL);
    lv_label_set_text(title, "Blocks of Code");
    lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_align(title, header, LV_ALIGN_IN_LEFT_MID, 8, 0);

    // Time label (right)
    time_label = lv_label_create(header, NULL);
    lv_label_set_text(time_label, "--:-- --");
    lv_obj_set_style_local_text_color(time_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_align(time_label, header, LV_ALIGN_IN_RIGHT_MID, -8, 0);

    update_time_label();

    // ---------- Tabview (below header) ----------
    lv_obj_t *tv = lv_tabview_create(scr, NULL);

    // Move the tabview down below header
    lv_obj_set_pos(tv, 0, 32);
    lv_obj_set_size(tv, 320, 240 - 32);

    // Optional: make tab background match theme
    lv_obj_set_style_local_bg_color(tv, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xF4B6D8));
    lv_obj_set_style_local_bg_opa(tv, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);

    // Tabs
    lv_obj_t *tab_home   = lv_tabview_add_tab(tv, "Home");
    lv_obj_t *tab_status = lv_tabview_add_tab(tv, "Status");
    lv_obj_t *tab_about  = lv_tabview_add_tab(tv, "About");

    // ----- HOME TAB -----
    lv_obj_t *welcome = lv_label_create(tab_home, NULL);
    lv_label_set_text(welcome, "Welcome to\nBlocks of Code");
    lv_obj_set_style_local_text_color(welcome, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_label_set_align(welcome, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(welcome, NULL, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *sub = lv_label_create(tab_home, NULL);
    lv_label_set_text(sub, "Brain Block UI");
    lv_obj_set_style_local_text_color(sub, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_label_set_align(sub, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(sub, NULL, LV_ALIGN_CENTER, 0, 25);

    // ----- STATUS TAB -----
    lv_obj_t *status = lv_label_create(tab_status, NULL);
    lv_label_set_text(status, "Status:\nWiFi: TBD\nBlocks: TBD");
    lv_obj_set_style_local_text_color(status, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_align(status, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);

    // ----- ABOUT TAB -----
    lv_obj_t *about = lv_label_create(tab_about, NULL);
    lv_label_set_text(about, "Blocks of Code (v3)\nUCF Senior Design\nGroup 28");
    lv_obj_set_style_local_text_color(about, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_align(about, NULL, LV_ALIGN_IN_TOP_LEFT, 10, 10);

    // ---------- LVGL loop + time update ----------
    int ms = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_task_handler();

        ms += 10;
        if (ms >= 1000) {
            ms = 0;
            update_time_label();
        }
    }
}



void time_sync_notification_cb(struct timeval *tv)
{
   	ESP_LOGI(wifi_tag, "Notification of a time synchronization event");
}

/*static void initialize_sntp(void)
{
	ESP_LOGI(wifi_tag, "Initializing SNTP");
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, "pool.ntp.org");
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();

} */

 /*void button_push_handler(void* arg)
{
	while(1)
	{
		int value = gpio_get_level(PUSH_BUTTON);
		if (value == 1)
			gpio_set_level(LCD_BACKLIGHT,1);
		else if(value == 0)
	  		gpio_set_level(LCD_BACKLIGHT,0);

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
} */





static void lv_tick_task(void *arg) 
{
    (void) arg;
    lv_tick_inc(LV_TICK_PERIOD_MS);
}



/*static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI(wifi_tag,"Connecting to AP....");
		esp_wifi_connect();
	}
	else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if(s_retry_num < MAX_FAILURES)
		{
			ESP_LOGI(wifi_tag,"Reconnecting to AP....");
			esp_wifi_connect();
			s_retry_num++;

		}	
		else
		{
			ESP_LOGI(wifi_tag,"Could not connect to WIFI!!");
			xEventGroupSetBits(wifi_event_group,WIFI_FAILURE);
		}
	}
} */

/* static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(wifi_tag,"STA IP: " IPSTR,IP2STR(&event->ip_info.ip));
		s_retry_num=0;
		xEventGroupSetBits(wifi_event_group,WIFI_SUCCESS);
		
	}
} */


 /* static int initialise_wifi(void)
{

	int status = WIFI_FAILURE;
	ESP_ERROR_CHECK(esp_netif_init());
	
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	
	// create event group
	wifi_event_group = xEventGroupCreate();
	
	// set connect to wifi event handler	
	esp_event_handler_instance_t wifi_handler_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler,NULL,&wifi_handler_event_instance));

	// set obtained IP event handler
	esp_event_handler_instance_t got_ip_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&ip_event_handler,NULL,&got_ip_event_instance));

	
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "Destiny",
			.password="Poetry1129!",
			//.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			.pmf_cfg = {
				.capable=true,
				.required=false
			},
			},
		};

	// set wifi mode to wifi station	
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	// set the configuration
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
	// start the wifi
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(wifi_tag,"STA initialization complete");
	
	// wait until either WIFI_SUCCESS or WIFI_FAILURE bits are set in the wifi_event_group
	EventBits_t bits = xEventGroupWaitBits(wifi_event_group,WIFI_SUCCESS|WIFI_FAILURE,pdFALSE,pdFALSE,portMAX_DELAY);
	
	// check the set value inside bits
	if (bits & WIFI_SUCCESS)
		status = WIFI_SUCCESS;
	else if(bits & WIFI_FAILURE)
		status = WIFI_FAILURE;	
	else
	{
		ESP_LOGE(TAG,"Unexpected event");
		status = WIFI_FAILURE;
	}

	// deregister handlers and delete event group
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,got_ip_event_instance));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler_event_instance));
	vEventGroupDelete(wifi_event_group);

	return status;
} */


/*static int obtain_time(void)
{	
	if (initialise_wifi() == WIFI_SUCCESS)
		initialize_sntp();
	
	time_t now;
    	struct tm timeinfo;
    	time(&now);
    	localtime_r(&now, &timeinfo);

	int retry = 0;
    	const int retry_count = 20;

	while(timeinfo.tm_year < (2022 - 1900) && ++retry < retry_count) {
		vTaskDelay(500 / portTICK_PERIOD_MS);
	        time(&now);
	    	localtime_r(&now, &timeinfo);
	}

    	if (timeinfo.tm_year < (2022 - 1900)) {
    		ESP_LOGI(wifi_tag, "System time NOT set.");
		wifi_connect_status = WIFI_FAILURE;
    	}
    	else 
	{
    		ESP_LOGI(wifi_tag, "System time is set.");

		setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
tzset();

    		localtime_r(&now, &timeinfo);
		wifi_connect_status = WIFI_SUCCESS;
    	}
	return 1;
} */


void prvStatTask(void* para)
{
	xSemaphoreTake( tab_semaphore, portMAX_DELAY );
	display_system_runtime_stat(sys_stat_tab);

}


void tft_ui_start(void)
{
	clock_semaphore = xSemaphoreCreateMutex();
	tab_semaphore = xSemaphoreCreateBinary();


	lv_init();

    	lvgl_driver_init();

    	lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    	assert(buf1 != NULL);

    	lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    	assert(buf2 != NULL);

    	static lv_disp_buf_t disp_buf;

    	uint32_t size_in_px = DISP_BUF_SIZE;


    	/* Initialize the working buffer depending on the selected display.
     	* NOTE: buf2 == NULL when using monochrome displays. */
    	lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    	lv_disp_drv_t disp_drv;
    	lv_disp_drv_init(&disp_drv);
    	disp_drv.flush_cb = disp_driver_flush;

    	disp_drv.buffer = &disp_buf;
    	lv_disp_drv_register(&disp_drv);

    	/*lv_indev_drv_t indev_drv;
    	lv_indev_drv_init(&indev_drv);
    	indev_drv.read_cb = touch_driver_read;
    	indev_drv.type = LV_INDEV_TYPE_POINTER;
    	lv_indev_drv_register(&indev_drv);
	*/
	
    	/* Create and start a periodic timer interrupt to call lv_tick_inc */
    	const esp_timer_create_args_t periodic_timer_args = {
	        .callback = &lv_tick_task,
	        .name = "periodic_gui"
    	};

	esp_timer_handle_t periodic_timer;
	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));


	gpio_reset_pin(LCD_BACKLIGHT);
	gpio_set_direction(LCD_BACKLIGHT,GPIO_MODE_OUTPUT);	

	//BaseType_t push_button_check_handle = xTaskCreatePinnedToCore(button_push_handler, "CheckButtonStatus", 1024,  xTaskGetCurrentTaskHandle(), 10,NULL,0);
	//if(push_button_check_handle == pdPASS)
		//ESP_LOGI(TAG,"Push button check task created");

	//BaseType_t display_stat_handle = xTaskCreatePinnedToCore(prvStatTask, "DisplayRunTimeStat", 1024*2,  xTaskGetCurrentTaskHandle(), 10,NULL,0);
	//if(display_stat_handle == pdPASS)
		//ESP_LOGI(TAG,"Display Runtime Stat task created");

	// run LVGL GUI on core 1 of ESP32	
	BaseType_t gui_task_handle = xTaskCreatePinnedToCore(create_demo_application,"GUITask", 1024*4, NULL, 10, NULL, 1);
	if(gui_task_handle == pdPASS)
		ESP_LOGI(TAG,"GUI task created");

	//ESP_ERROR_CHECK( nvs_flash_init() );

	/*if (pdTRUE == xSemaphoreTake(clock_semaphore, portMAX_DELAY)) 
	{
		obtain_time();
		xSemaphoreGive(clock_semaphore);
	} */



    	//* A task should NEVER return */
    	//free(buf1);
    	//free(buf2);
    	//vTaskDelete(NULL);

}


