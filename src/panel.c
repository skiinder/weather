#include "panel.h"
#include "lvgl.h"
#include "memchunk.h"
#include <cjson/cJSON.h>
#include <time.h>
#include <stdio.h>
#include <curl/curl.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <jpeglib.h>

typedef struct WeatherDataStruct
{
    char temperature[10];
    char weather[40];
    char wind_direction[20];
    char wind_power[10];
    char humidity[10];
    pthread_mutex_t data_lock;
} WeatherData;

static size_t panel_writeMemchunkCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Memchunk *chunk = (Memchunk *)userp;
    if (memchunk_append(chunk, contents, realsize) < 0)
        return 0;
    return realsize;
}

static int panel_request(char *url, Memchunk *chunk)
{
    CURL *curl;
    CURLcode res;
    long response_code;

    curl = curl_easy_init();

    if (!curl)
        return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, panel_writeMemchunkCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);


    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        LV_LOG_WARN("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    if (response_code != 200)
    {
        LV_LOG_WARN("Curl response code %ld\n", response_code);
        return -1;
    }
    LV_LOG_INFO("Curl result: %s\n", chunk->ptr);
    return 0;
}

static int panel_parseWeatherData(char *json_str, WeatherData *weather_data)
{
    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL)
    {
        LV_LOG_ERROR("Error parsing JSON\n");
        return -1;
    }

    cJSON *lives = cJSON_GetObjectItem(json, "lives");
    if (lives == NULL || !cJSON_IsArray(lives))
    {
        LV_LOG_ERROR("Error getting lives array from JSON\n");
        cJSON_Delete(json);
        return -1;
    }

    cJSON *live = cJSON_GetArrayItem(lives, 0);
    if (live == NULL)
    {
        LV_LOG_ERROR("Error getting weather data from JSON\n");
        cJSON_Delete(json);
        return -1;
    }

    cJSON *temperature = cJSON_GetObjectItem(live, "temperature");
    cJSON *weather = cJSON_GetObjectItem(live, "weather");
    cJSON *winddirection = cJSON_GetObjectItem(live, "winddirection");
    cJSON *windpower = cJSON_GetObjectItem(live, "windpower");
    cJSON *humidity = cJSON_GetObjectItem(live, "humidity");

    if (!cJSON_IsString(temperature) || !cJSON_IsString(weather) ||
        !cJSON_IsString(winddirection) || !cJSON_IsString(windpower) || !cJSON_IsString(humidity))
    {
        LV_LOG_ERROR("Error getting weather data from JSON\n");
        cJSON_Delete(json);
        return -1;
    }

    pthread_mutex_lock(&weather_data->data_lock);
    strcpy(weather_data->temperature, temperature->valuestring);
    strcpy(weather_data->weather, weather->valuestring);
    strcpy(weather_data->wind_direction, winddirection->valuestring);
    strcpy(weather_data->wind_power, windpower->valuestring);
    strcpy(weather_data->humidity, humidity->valuestring);
    pthread_mutex_unlock(&weather_data->data_lock);

    cJSON_Delete(json);

    return 0;
}

static int panel_parseCityCode(char *json_str, char *city_code)
{
    cJSON *json = cJSON_Parse(json_str);

    if (json == NULL)
    {
        LV_LOG_ERROR("Error parsing JSON\n");
        return -1;
    }

    cJSON *adcode = cJSON_GetObjectItem(json, "adcode");
    if (adcode == NULL || !cJSON_IsString(adcode))
    {
        LV_LOG_ERROR("Error getting adcode array from JSON\n");
        cJSON_Delete(json);
        return -1;
    }

    LV_ASSERT(strlen(adcode->valuestring) == 6);
    strcpy(city_code, adcode->valuestring);

    cJSON_Delete(json);
    return 0;
}

static void *panel_getWeather(void *ptr)
{
    Memchunk chunk;
    char city_code[7];
    char weather_url[256];
    memset(&chunk, 0, sizeof(Memchunk));
    if (memchunk_init(&chunk) < 0)
    {
        return (void *)-1;
    }

    if (panel_request("http://restapi.amap.com/v3/ip?key=543b7b6c08e25803d4adf379f2b68d50", &chunk) < 0)
    {
        goto FAIL_EXIT;
    }

    if (panel_parseCityCode(chunk.ptr, city_code) < 0)
    {
        goto FAIL_EXIT;
    }

    snprintf(weather_url, sizeof(weather_url), "http://restapi.amap.com/v3/weather/weatherInfo?key=543b7b6c08e25803d4adf379f2b68d50&city=%s&extensions=base", city_code);

    memchunk_init(&chunk);

    if (panel_request(weather_url, &chunk) < 0)
    {
        goto FAIL_EXIT;
    }

    WeatherData *weather_data = ptr;

    if (panel_parseWeatherData(chunk.ptr, weather_data) < 0)
    {
        goto FAIL_EXIT;
    }

    memchunk_free(&chunk);
    return (void *)0;

FAIL_EXIT:
    memchunk_free(&chunk);
    return (void *)-1;
}

static void panel_updateData(lv_timer_t *lv_timer)
{
    lv_obj_t *panel = lv_timer->user_data;
    WeatherData *weather_data = panel->user_data;
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);

    if (timeinfo == NULL)
    {
        return;
    }

    char date_buffer[64];
    char time_buffer[64];
    char temprature_buffer[128];
    char humidity_buffer[128];
    char wind_buffer[128];
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", timeinfo);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", timeinfo);
    lv_label_set_text(lv_obj_get_child(panel, 0), time_buffer);
    lv_label_set_text(lv_obj_get_child(panel, 1), date_buffer);

    pthread_mutex_lock(&weather_data->data_lock);
    snprintf(temprature_buffer, sizeof(temprature_buffer), "温度: %s°C", weather_data->temperature);
    snprintf(wind_buffer, sizeof(wind_buffer), "风向: %s, 风力: %s", weather_data->wind_direction, weather_data->wind_power);
    snprintf(humidity_buffer, sizeof(humidity_buffer), "湿度: %s", weather_data->humidity);
    lv_label_set_text(lv_obj_get_child(panel, 2), weather_data->weather);
    pthread_mutex_unlock(&weather_data->data_lock);
    lv_label_set_text(lv_obj_get_child(panel, 3), temprature_buffer);
    lv_label_set_text(lv_obj_get_child(panel, 4), wind_buffer);
    lv_label_set_text(lv_obj_get_child(panel, 5), humidity_buffer);
}

static void panel_updateWeather(lv_timer_t *lv_timer)
{
    pthread_t thread = 0;
    lv_obj_t *panel = lv_timer->user_data;
    if (pthread_create(&thread, NULL, panel_getWeather, panel->user_data) > 0)
    {
        pthread_detach(thread);
    }
}

lv_obj_t *panel_create(void)
{
    // 声明外部字体
    LV_FONT_DECLARE(lv_font_chinese_18);
    // 声明背景
    LV_IMG_DECLARE(background);

    // Create a new screen
    lv_obj_t *parent_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_width(parent_obj, LV_HOR_RES);
    lv_obj_set_height(parent_obj, LV_VER_RES);
    WeatherData *temp = lv_mem_alloc(sizeof(WeatherData));
    if (!temp)
    {
        LV_LOG_ERROR("Failed to allocate memory for WeatherData\n");
        exit(EXIT_FAILURE);
    }

    strcpy(temp->temperature, "--");
    strcpy(temp->humidity, "--");
    strcpy(temp->weather, "----");
    strcpy(temp->wind_direction, "----");
    strcpy(temp->wind_power, "----");
    pthread_mutex_init(&temp->data_lock, NULL);

    parent_obj->user_data = temp;

    lv_obj_set_style_bg_img_src(parent_obj, &background, 0);

    // Time display
    lv_obj_t *label_time = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_48, 0);
    lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -70);

    // Date display
    lv_obj_t *label_date = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_date, &lv_font_chinese_18, 0);
    lv_obj_align(label_date, LV_ALIGN_CENTER, 0, -40);

    // Weather description
    lv_obj_t *label_weather = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_weather, &lv_font_chinese_18, 0);
    lv_obj_align(label_weather, LV_ALIGN_CENTER, 60, 0);

    // Current temperature
    lv_obj_t *label_temprature = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_temprature, &lv_font_chinese_18, 0);
    lv_obj_align(label_temprature, LV_ALIGN_CENTER, 60, 20);

    // Wind level
    lv_obj_t *label_wind = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_wind, &lv_font_chinese_18, 0);
    lv_obj_align(label_wind, LV_ALIGN_CENTER, 60, 40);

    // Humidity
    lv_obj_t *label_humidity = lv_label_create(parent_obj);
    lv_obj_set_style_text_font(label_humidity, &lv_font_chinese_18, 0);
    lv_obj_align(label_humidity, LV_ALIGN_CENTER, 60, 60);

    lv_timer_t *timer1 = lv_timer_create(panel_updateData, 1000, parent_obj);
    lv_timer_t *timer2 = lv_timer_create(panel_updateWeather, 60 * 1000, parent_obj);

    panel_updateData(timer1);
    panel_updateWeather(timer2);
    return parent_obj;
}