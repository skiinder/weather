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

// 全局变量定义
lv_obj_t *label_time;
lv_obj_t *label_date;
lv_obj_t *label_weather;
lv_obj_t *label_temprature;
lv_obj_t *label_wind;
lv_obj_t *label_humidity;

typedef struct WeatherDataStruct
{
    char temperature[10];
    char weather[40];
    char wind_direction[20];
    char wind_power[10];
    char humidity[10];
} WeatherData;

static WeatherData data = {
    .humidity = "--",
    .temperature = "--",
    .weather = "----",
    .wind_direction = "----",
    .wind_power = "---"};
pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t weather_thread = 0;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
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

    pthread_mutex_lock(&data_lock);
    strcpy(weather_data->temperature, temperature->valuestring);
    strcpy(weather_data->weather, weather->valuestring);
    strcpy(weather_data->wind_direction, winddirection->valuestring);
    strcpy(weather_data->wind_power, windpower->valuestring);
    strcpy(weather_data->humidity, humidity->valuestring);
    pthread_mutex_unlock(&data_lock);

    cJSON_Delete(json);

    return 0;
}

static void *panel_getWeather(void *ptr)
{
    Memchunk chunk;
    if (memchunk_init(&chunk) < 0)
    {
        return (void *)-1;
    }

    if (panel_request("http://restapi.amap.com/v3/weather/weatherInfo?key=543b7b6c08e25803d4adf379f2b68d50&city=110000&extensions=base",
                      &chunk) < 0)
    {
        goto FAIL_EXIT;
    }

    LV_LOG_INFO("Curl result: %s\n", chunk.ptr);

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

static void update_data(lv_timer_t *lv_timer)
{
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
    lv_label_set_text(label_date, date_buffer);
    lv_label_set_text(label_time, time_buffer);

    pthread_mutex_lock(&data_lock);
    snprintf(temprature_buffer, sizeof(temprature_buffer), "温度: %s°C", data.temperature);
    snprintf(wind_buffer, sizeof(wind_buffer), "风向: %s, 风力: %s", data.wind_direction, data.wind_power);
    snprintf(humidity_buffer, sizeof(humidity_buffer), "湿度: %s", data.humidity);
    lv_label_set_text(label_weather, data.weather);
    pthread_mutex_unlock(&data_lock);
    lv_label_set_text(label_temprature, temprature_buffer);
    lv_label_set_text(label_wind, wind_buffer);
    lv_label_set_text(label_humidity, humidity_buffer);
}

static void update_weather(lv_timer_t *lv_timer)
{
    if (pthread_create(&weather_thread, NULL, panel_getWeather, &data) > 0)
    {
        pthread_detach(weather_thread);
    }
}

void panel_create(void)
{
    // 声明外部字体
    LV_FONT_DECLARE(lv_font_chinese_18);
    // 声明背景
    LV_IMG_DECLARE(background);

    // Create a new screen
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_img_src(scr, &background, 0);

    // Time display
    label_time = lv_label_create(scr);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_48, 0);
    lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -70);

    // Date display
    label_date = lv_label_create(scr);
    lv_obj_set_style_text_font(label_date, &lv_font_chinese_18, 0);
    lv_obj_align(label_date, LV_ALIGN_CENTER, 0, -40);

    // Weather description
    label_weather = lv_label_create(scr);
    lv_obj_set_style_text_font(label_weather, &lv_font_chinese_18, 0);
    lv_obj_align(label_weather, LV_ALIGN_CENTER, 60, 0);

    // Current temperature
    label_temprature = lv_label_create(scr);
    lv_obj_set_style_text_font(label_temprature, &lv_font_chinese_18, 0);
    lv_obj_align(label_temprature, LV_ALIGN_CENTER, 60, 20);

    // Wind level
    label_wind = lv_label_create(scr);
    lv_obj_set_style_text_font(label_wind, &lv_font_chinese_18, 0);
    lv_obj_align(label_wind, LV_ALIGN_CENTER, 60, 40);

    // Humidity
    label_humidity = lv_label_create(scr);
    lv_obj_set_style_text_font(label_humidity, &lv_font_chinese_18, 0);
    lv_obj_align(label_humidity, LV_ALIGN_CENTER, 60, 60);

    update_data(NULL);
    update_weather(NULL);

    lv_timer_create(update_data, 1000, NULL);
    lv_timer_create(update_weather, 60 * 1000, NULL);
}