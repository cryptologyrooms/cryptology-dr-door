#include "raat.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

#define MAX_PATTERN_PIXELS 20

typedef enum _ePatternState
{
    eState_Idle,
    eState_Running,
    eState_Fading,
} ePatternState;

typedef struct app_data
{
    raat_devices_struct* pDevices;
    raat_params_struct* pParams;
    ePatternState pattern_state;
    uint8_t pattern_position;
    int8_t pattern_end_position;
    uint8_t pattern_pixels[MAX_PATTERN_PIXELS][3];
    uint8_t fader[3];
} APP_DATA;

static APP_DATA s_app_data = {
    NULL, NULL,
    eState_Idle,
    0,
    0,
    {0}
};

static void draw_pattern(APP_DATA& app_data)
{
    uint8_t rgb[3];
    uint8_t change[3];
    uint8_t length = app_data.pParams->pPatternLength->get();
    memset(app_data.pattern_pixels,0, MAX_PATTERN_PIXELS*3);

    app_data.pParams->pStartColour->get(rgb);
    app_data.pParams->pStartColour->get(app_data.fader);

    change[0] = (rgb[0] - app_data.pParams->pEndColour->get(eR)) / (length-1);
    change[1] = (rgb[1] - app_data.pParams->pEndColour->get(eG)) / (length-1);
    change[2] = (rgb[2] - app_data.pParams->pEndColour->get(eB)) / (length-1);

    for (uint8_t i = 0; i < length; i++)
    {
        app_data.pattern_pixels[i][0] = rgb[0];
        app_data.pattern_pixels[i][1] = rgb[1];
        app_data.pattern_pixels[i][2] = rgb[2];
        rgb[0] -= change[0];
        rgb[1] -= change[1];
        rgb[2] -= change[2];
    }
}

static void pattern_task_fn(RAATTask& task, void * pTaskData)
{
    APP_DATA * pAppData = (APP_DATA*)pTaskData;
    AdafruitNeoPixelRAAT * pPixels = pAppData->pDevices->pNeoPixels;
    uint8_t length = pAppData->pParams->pPatternLength->get();

    pPixels->clear();

    switch(pAppData->pattern_state)
    {
    case eState_Idle:
        break;
    case eState_Running:
        for (uint8_t i = 0; i<length; i++)
        {
            pPixels->setPixelColor(
                pAppData->pattern_position-i,
                pAppData->pattern_pixels[i]
            );
        }

        if (pAppData->pattern_position >= 31)
        {
            for (uint8_t i = 31; i<=pAppData->pattern_position; i++)
            {
                pPixels->setPixelColor(
                    i,
                    pAppData->pattern_pixels[0]
                );
            }
        }

        (pAppData->pattern_position)++;

        if (pAppData->pattern_position >= (40+length))
        {
            pAppData->pattern_state = eState_Fading;
            task.set_period(pAppData->pParams->pFadeInterval->get());
        }
        break;

    case eState_Fading:
        pAppData->fader[0] = (uint8_t)(((uint16_t)pAppData->fader[0] * 99U) / 100U);
        pAppData->fader[1] = (uint8_t)(((uint16_t)pAppData->fader[1] * 99U) / 100U);
        pAppData->fader[2] = (uint8_t)(((uint16_t)pAppData->fader[2] * 99U) / 100U);

        for (uint8_t i = 31; i<=41; i++)
        {
            pPixels->setPixelColor(
                i,
                pAppData->fader[0],
                pAppData->fader[1],
                pAppData->fader[2]
            );
        }

        if ((pAppData->fader[0] == 0) && (pAppData->fader[1] == 0) && (pAppData->fader[2] == 0))
        {
            pAppData->pattern_state = eState_Idle;
        }
        break;
    }
    pPixels->show();
}
static RAATTask s_pattern_task(500, pattern_task_fn, &s_app_data);

static void start_pattern(APP_DATA& app_data)
{
    draw_pattern(app_data);
    s_pattern_task.set_period(app_data.pParams->pPatternInterval->get());
    app_data.pattern_state = eState_Running;
    app_data.pattern_position = 0;
    app_data.pattern_end_position = (-app_data.pParams->pPatternLength->get()) + 1;
}

void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    s_app_data.pDevices = &devices;
    s_app_data.pParams = &params;
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices; (void)params;


    if (devices.pTest_Button->check_low_and_clear())
    {
        if (s_app_data.pattern_state == eState_Idle)
        {
            raat_logln(LOG_APP, "Starting pattern (button press)");
            start_pattern(s_app_data);
        }
    }

    while(s_app_data.pattern_state != eState_Idle)
    {
        s_pattern_task.run();
        if (s_app_data.pattern_state == eState_Idle)
        {
            raat_logln(LOG_APP, "Pattern done");
        }
    }
}
