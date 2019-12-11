#include "raat.hpp"

#include "raat-oneshot-timer.hpp"
#include "raat-oneshot-task.hpp"
#include "raat-task.hpp"

#define MAX_PATTERN_PIXELS 20

#define JOIN_1_PIXEL 26
#define JOIN_2_PIXEL 30
#define END_PIXEL 40
#define OFF_END_PIXEL 41

typedef enum _ePatternState
{
    eState_Idle,
    eState_Running1,
    eState_MidRunDelay,
    eState_Running2,
    eState_Fading,
} ePatternState;

typedef struct run_data
{
    AdafruitNeoPixelRAAT * pPixels;
    ePatternState pattern_state;
    uint16_t pattern_interval;
    uint8_t pattern_position;
    uint8_t length;
    uint8_t join_1_counter;
    float fade_increments[3];
    uint16_t fade_frames;
    uint16_t fade_frame_count;
    uint8_t fade_count;
    bool fade_down;
    RGBParam<uint8_t> * start_colour;
    RGBParam<uint8_t> * end_colour;
    uint16_t delay;
    uint8_t pattern_pixels[MAX_PATTERN_PIXELS][3];
    uint8_t fader[3];
} RUN_DATA;

typedef struct rfid_data
{
    RFID_RC522 * pRFIDReader;
    bool rfid_present;
} RFID_DATA;

static void draw_pattern(RUN_DATA& run_data)
{
    uint8_t rgb[3];
    uint8_t change[3];

    run_data.start_colour->get(rgb);

    change[0] = (rgb[0] - run_data.end_colour->get(eR)) / (run_data.length-1);
    change[1] = (rgb[1] - run_data.end_colour->get(eG)) / (run_data.length-1);
    change[2] = (rgb[2] - run_data.end_colour->get(eB)) / (run_data.length-1);

    for (uint8_t i = 0; i < run_data.length; i++)
    {
        run_data.pattern_pixels[i][0] = rgb[0];
        run_data.pattern_pixels[i][1] = rgb[1];
        run_data.pattern_pixels[i][2] = rgb[2];
        rgb[0] -= change[0];
        rgb[1] -= change[1];
        rgb[2] -= change[2];
    }
}

static void pattern_task_fn(RAATTask& task, void * pTaskData)
{
    RUN_DATA& run_data = *(RUN_DATA*)pTaskData;

    run_data.pPixels->clear();

    switch(run_data.pattern_state)
    {

    case eState_Idle:
        break;

    case eState_Running1:
        if (run_data.pattern_position < JOIN_1_PIXEL)
        {
            for (uint8_t i = 0; i<run_data.length; i++)
            {
                run_data.pPixels->setPixelColor(
                    run_data.pattern_position-i,
                    run_data.pattern_pixels[i]
                );
            }
            run_data.pattern_position++;
        }
        else
        {
            for (uint8_t i = 0; i<run_data.join_1_counter; i++)
            {
                const uint8_t px = run_data.pattern_position-i;
                run_data.pPixels->setPixelColor(px, run_data.pattern_pixels[i]);
            }

            run_data.pPixels->setPixelColor(JOIN_1_PIXEL, run_data.pattern_pixels[0]);
            run_data.join_1_counter--;
            if (run_data.join_1_counter == 0)
            {
                if (run_data.delay)
                {
                    run_data.pattern_state = eState_MidRunDelay;
                    task.set_period(run_data.delay);
                }
                else
                {
                    run_data.pattern_state = eState_Running2;
                }
            }
        }
        break;

    case eState_MidRunDelay:
        task.set_period(run_data.pattern_interval);
        run_data.pattern_state = eState_Running2;
        break;

    case eState_Running2:
        for (uint8_t i = 0; i<run_data.length; i++)
        {
            const uint8_t px = run_data.pattern_position-i;
            if (px >= JOIN_1_PIXEL)
            {
                run_data.pPixels->setPixelColor(
                    px,
                    run_data.pattern_pixels[i]
                );
            }
        }

        if (run_data.pattern_position >= (JOIN_2_PIXEL+1))
        {
            for (uint8_t i = (JOIN_2_PIXEL+1); i<=run_data.pattern_position; i++)
            {
                run_data.pPixels->setPixelColor(
                    i,
                    run_data.pattern_pixels[0]
                );
            }
        }

        (run_data.pattern_position)++;

        if (run_data.pattern_position >= (END_PIXEL+run_data.length))
        {
            run_data.pattern_state = eState_Fading;
            task.set_period(50);
        }
        break;

    case eState_Fading:
        if (run_data.fade_down)
        {
            run_data.fader[0] = subtract_with_limit((float)run_data.fader[0], run_data.fade_increments[0], 0.0f);
            run_data.fader[1] = subtract_with_limit((float)run_data.fader[1], run_data.fade_increments[1], 0.0f);
            run_data.fader[2] = subtract_with_limit((float)run_data.fader[2], run_data.fade_increments[2], 0.0f);
        }
        else
        {
            run_data.fader[0] = add_with_limit((float)run_data.fader[0], run_data.fade_increments[0], 255.0f);
            run_data.fader[1] = add_with_limit((float)run_data.fader[1], run_data.fade_increments[1], 255.0f);
            run_data.fader[2] = add_with_limit((float)run_data.fader[2], run_data.fade_increments[2], 255.0f);         
        }

        for (uint8_t i = (JOIN_2_PIXEL+1); i<=OFF_END_PIXEL; i++)
        {
            run_data.pPixels->setPixelColor(i, run_data.fader);
        }

        if (run_data.fade_down)
        {
            if (--run_data.fade_frame_count == 0)
            {
                run_data.fade_count--;
                if (run_data.fade_count == 0)
                {
                    run_data.pattern_state = eState_Idle;
                }
                else
                {
                    run_data.fade_down = false;
                    run_data.fade_frame_count = run_data.fade_frames;
                }
            }
        }
        else
        {
            if (--run_data.fade_frame_count == 0)
            {
                run_data.fade_down = true;
                run_data.fade_frame_count = run_data.fade_frames;
                run_data.start_colour->get(run_data.fader);
            }
        }

        break;
    }
    run_data.pPixels->show();
}
static RAATTask s_pattern_task(500, pattern_task_fn, NULL);

static void init_run_data(const raat_devices_struct& devices, const raat_params_struct& params, RUN_DATA& run_data)
{
    float fade_frames = (params.pFadeInterval->get() * 25) / 1000;

    run_data.pPixels = devices.pNeoPixels;
    run_data.pattern_state = eState_Running1;
    run_data.pattern_interval = params.pPatternInterval->get();
    run_data.pattern_position = 0;
    run_data.length = params.pPatternLength->get();
    run_data.join_1_counter = run_data.length;
    run_data.fade_increments[0] = params.pStartColour->get(eR) / fade_frames;
    run_data.fade_increments[1] = params.pStartColour->get(eG) / fade_frames;
    run_data.fade_increments[2] = params.pStartColour->get(eB) / fade_frames;
    run_data.fade_frames = fade_frames;
    run_data.fade_frame_count = fade_frames;
    run_data.fade_count = params.pFadeCount->get() + 1; // Add one for initial fadeout
    run_data.fade_down = true;
    run_data.start_colour = params.pStartColour;
    run_data.end_colour = params.pEndColour;
    run_data.delay = params.pDelay->get();
    memset(run_data.pattern_pixels,0, MAX_PATTERN_PIXELS*3);
    run_data.start_colour->get(run_data.fader);
}

static void start_pattern(const raat_devices_struct& devices, const raat_params_struct& params, RUN_DATA& run_data)
{
    init_run_data(devices, params, run_data);
    draw_pattern(run_data);
    s_pattern_task.set_period(run_data.pattern_interval);

}

static void rfid_task_fn(RAATTask& task, void * pTaskData)
{
    (void)task;
    char uid[12];
    RFID_DATA* pRFIDData = (RFID_DATA*)pTaskData;
    pRFIDData->rfid_present = pRFIDData->pRFIDReader->get(uid);
}
static RAATTask s_rfid_task(250, rfid_task_fn, NULL);

void raat_custom_setup(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices;
    (void)params;
}

void raat_custom_loop(const raat_devices_struct& devices, const raat_params_struct& params)
{
    (void)devices; (void)params;

    static RUN_DATA s_run_data = {
        .pPixels = NULL,
        .pattern_state = eState_Idle,
        .pattern_interval = 0,
        .pattern_position = 0,
        .length = 0,
        .join_1_counter = 0,
        .fade_increments = {0.0f, 0.0f, 0.0f},
        .fade_frames = 0,
        .fade_frame_count = 0,
        .fade_count = 0,
        .fade_down = true,
        .start_colour = NULL,
        .end_colour = NULL,
        .delay = 0,
        .pattern_pixels = {0},
        .fader = {0}
    };

    static RFID_DATA s_rfid_data = {
        .pRFIDReader = devices.pRFID,
        .rfid_present = false
    };

    if (s_run_data.pattern_state == eState_Idle)
    {
        s_rfid_task.run(&s_rfid_data);

        if (check_and_clear(s_rfid_data.rfid_present))
        {
            raat_logln(LOG_APP, "Starting pattern (button press)");
            start_pattern(devices, params, s_run_data);
        }
        else if (devices.pTest_Button->check_low_and_clear())
        {
            raat_logln(LOG_APP, "Starting pattern (button press)");
            start_pattern(devices, params, s_run_data);
        }
    }

    while(s_run_data.pattern_state != eState_Idle)
    {
        s_pattern_task.run(&s_run_data);
        if (s_run_data.pattern_state == eState_Idle)
        {
            raat_logln(LOG_APP, "Pattern done");
            devices.pOutput_Relay->set(true, 5000);
        }
    }
}
