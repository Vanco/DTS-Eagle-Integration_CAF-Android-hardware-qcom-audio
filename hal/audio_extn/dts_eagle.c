/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_dts_eagle"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <sound/asound.h>
#include <sound/audio_effects.h>

#define DTS_EAGLE_KEY       "DTS_EAGLE"
static const char* DTS_EAGLE_STR = DTS_EAGLE_KEY;

#ifdef DTS_EAGLE_ENABLED
#define AUDIO_PARAMETER_KEY_DTS_EAGLE "DTS_EAGLE"

struct dts_eagle_param_desc_alsa {
    int alsa_effect_ID;
    struct dts_eagle_param_desc d;
};

static struct dts_eagle_param_desc_alsa *fade_in_data = NULL;
static struct dts_eagle_param_desc_alsa *fade_out_data = NULL;

static int do_DTS_Eagle_params_stream(struct stream_out *out, struct dts_eagle_param_desc_alsa *t) {
    char mixer_string[128];
    struct mixer_ctl *ctl;
    int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);

    ALOGI("%s: enter", __func__);

    snprintf(mixer_string, sizeof(mixer_string), "%s %d", "Audio Effects Config", pcm_device_id);
    ctl = mixer_get_ctl_by_name(out->dev->mixer, mixer_string);
    if (!ctl) {
        ALOGE("DTS_EAGLE_HAL (%s) Failed to open mixer %s", __func__, mixer_string);
    } else {
        int size = t->d.size + sizeof(struct dts_eagle_param_desc_alsa);
        ALOGD("DTS_EAGLE_HAL (%s) Opened mixer %s", __func__, mixer_string);
        return mixer_ctl_set_array(ctl, t, size);
    }
    return -EINVAL;
}

static int do_DTS_Eagle_params(const struct audio_device *adev, struct dts_eagle_param_desc_alsa *t) {
    struct listnode *node;
    struct audio_usecase *usecase;
    int ret = 0;

    ALOGI("%s: enter", __func__);

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK) {
            int tret = do_DTS_Eagle_params_stream(usecase->stream.out, t);
                if (tret < 0)
                    ret = tret;
        }
    }
    return ret;
}

void audio_extn_dts_eagle_set_parameters(struct audio_device *adev, struct str_parms *parms) {
    int ret, val;
    char value[32] = { 0 };

    ALOGI("%s: enter", __func__);

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_DTS_EAGLE, value, sizeof(value));
    if (ret >= 0) {
        int *data = NULL, id, size, offset, count, dev, dts_found = 0, fade_in = 0;
        struct dts_eagle_param_desc_alsa *t2 = NULL, **t = &t2;

        ret = str_parms_get_str(parms, "fade", value, sizeof(value));
        if (ret >= 0) {
            fade_in = atoi(value);
            if (fade_in > 0) {
                t = (fade_in == 1) ? (struct dts_eagle_param_desc_alsa**)&fade_in_data : (struct dts_eagle_param_desc_alsa**)&fade_out_data;
            }
        }

        ret = str_parms_get_str(parms, "count", value, sizeof(value));
        if (ret >= 0) {
            count = atoi(value);
            if (count > 1) {
                int tmp_size = count * 32;
                char *tmp = malloc(tmp_size+1);
                data = malloc(sizeof(int) * count);
                ALOGI("DTS_EAGLE_HAL multi count param detected, count: %d", count);
                if (data && tmp) {
                    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_DTS_EAGLE, tmp, tmp_size);
                    if (ret >= 0) {
                        int idx = 0, tidx, tcnt = 0;
                        dts_found = 1;
                        do {
                            sscanf(&tmp[idx], "%i", &data[tcnt]);
                            tidx = strcspn(&tmp[idx], ",");
                            if(idx + tidx >= ret && tcnt < count-1) {
                                ALOGE("DTS_EAGLE_HAL malformed multi value string.");
                                dts_found = 0;
                                break;
                            }
                            ALOGD("DTS_EAGLE_HAL %i:%i (next %s)", tcnt, data[tcnt], &tmp[idx+tidx]);
                            idx += tidx + 1;
                            tidx = 0;
                            tcnt++;
                        } while(tcnt < count);
                    }
                } else {
                    ALOGE("DTS_EAGLE_HAL mem alloc for multi count param parse failed.");
                }
                if (tmp)
                    free(tmp);
            } else {
                data = malloc(sizeof(int));
                if (data) {
                    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_DTS_EAGLE, value, sizeof(value));
                    if (ret >= 0) {
                        *data = atoi(value);
                        dts_found = 1;
                        count = 1;
                    } else {
                        ALOGE("DTS_EAGLE_HAL malformed value string.");
                    }
                } else {
                    ALOGE("DTS_EAGLE_HAL mem alloc for param parse failed.");
                }
            }

            if (dts_found) {
                dts_found = 0;
                ret = str_parms_get_str(parms, "id", value, sizeof(value));
                if (ret >= 0) {
                    if(sscanf(value, "%x", &id) == 1) {
                        ret = str_parms_get_str(parms, "size", value, sizeof(value));
                        if (ret >= 0) {
                            size = atoi(value);
                            ret = str_parms_get_str(parms, "offset", value, sizeof(value));
                            if (ret >= 0) {
                                offset = atoi(value);
                                ret = str_parms_get_str(parms, "device", value, sizeof(value));
                                if (ret >= 0) {
                                    dev = atoi(value);
                                    dts_found = 1;
                                }
                            }
                        }
                    }
                }
            }

            if(size != (int)(count * sizeof(int)) && dts_found) {
                ALOGE("DTS_EAGLE_HAL size/count mismatch (size = %i bytes, count = %i integers/%i bytes).", size, count, count*sizeof(int));
            } else if (dts_found) {
                ALOGI("DTS_EAGLE_HAL param detected: %s", str_parms_to_str(parms));
                if (!(*t))
                    *t = (struct dts_eagle_param_desc_alsa*)malloc(sizeof(struct dts_eagle_param_desc_alsa) + size);
                if(*t) {
                    (*t)->alsa_effect_ID = DTS_EAGLE_MODULE;
                    (*t)->d.id = id;
                    (*t)->d.size = size;
                    (*t)->d.offset = offset;
                    (*t)->d.device = dev;
                    memcpy((void*)((char*)*t + sizeof(struct dts_eagle_param_desc_alsa)), data, size);
                    ALOGD("DTS_EAGLE_HAL id: 0x%X, size: %d, offset: %d, device: %d",
                           (*t)->d.id, (*t)->d.size, (*t)->d.offset, (*t)->d.device);
                    if (!fade_in) {
                        ret = do_DTS_Eagle_params(adev, *t);
                        if (ret < 0)
                            ALOGE("DTS_EAGLE_HAL failed setting params in kernel with error %i", ret);
                    }
                    free(t2);
                } else {
                    ALOGE("DTS_EAGLE_HAL mem alloc for dsp structure failed.");
                }
            } else {
                ALOGE("DTS_EAGLE_HAL param detected but failed parse: %s", str_parms_to_str(parms));
            }
            free(data);
        }
    }

exit:
    ALOGV("%s: exit", __func__);
}

int audio_extn_dts_eagle_get_parameters(const struct audio_device *adev,
                  struct str_parms *query, struct str_parms *reply) {
    int ret, val;
    char value[32] = { 0 };

    ALOGI("%s: enter", __func__);

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_DTS_EAGLE, value, sizeof(value));
    if (ret >= 0) {
        int *data = NULL, id, size, offset, count = 1, dev, idx = 0, dts_found = 0, i;
        const size_t chars_4_int = 16;
        ret = str_parms_get_str(query, "count", value, sizeof(value));
        if (ret >= 0) {
            count = atoi(value);
            if (count > 1) {
                ALOGI("DTS_EAGLE_HAL multi count param detected, count: %d", count);
            } else {
                count = 1;
            }
        }

        ret = str_parms_get_str(query, "id", value, sizeof(value));
        if (ret >= 0) {
            if(sscanf(value, "%x", &id) == 1) {
                ret = str_parms_get_str(query, "size", value, sizeof(value));
                if (ret >= 0) {
                    size = atoi(value);
                    ret = str_parms_get_str(query, "offset", value, sizeof(value));
                    if (ret >= 0) {
                        offset = atoi(value);
                        ret = str_parms_get_str(query, "device", value, sizeof(value));
                        if (ret >= 0) {
                            dev = atoi(value);
                            dts_found = 1;
                        }
                    }
                }
            }
        }

        if (dts_found) {
            ALOGI("DTS_EAGLE_HAL param (get) detected: %s", str_parms_to_str(query));
            struct dts_eagle_param_desc_alsa *t = (struct dts_eagle_param_desc_alsa*)malloc(sizeof(struct dts_eagle_param_desc_alsa) + size);
            if(t) {
                char buf[chars_4_int*count];
                t->alsa_effect_ID = DTS_EAGLE_MODULE;
                t->d.id = id;
                t->d.size = size;
                t->d.offset = offset;
                t->d.device = dev | 0x80000000/*trigger get*/;
                ALOGD("DTS_EAGLE_HAL id (get): 0x%X, size: %d, offset: %d, device: %d",
                       t->d.id, t->d.size, t->d.offset, t->d.device & 0x7FFFFFFF);
                ret = do_DTS_Eagle_params(adev, t);
                if (ret >= 0) {
                    data = (int*)((char*)t + sizeof(struct dts_eagle_param_desc_alsa));
                    for (i = 0; i < count; i++)
                        idx += snprintf(&buf[idx], chars_4_int, "%i,", data[i]);
                    buf[idx > 0 ? idx-1 : 0] = 0;
                    ALOGD("DTS_EAGLE_HAL get result: %s", buf);
                    strcpy(reply, buf);
                } else {
                    ALOGE("DTS_EAGLE_HAL failed getting params from kernel with error %i", ret);
                }
                free(t);
            } else {
                ALOGE("DTS_EAGLE_HAL mem alloc for (get) dsp structure failed.");
            }
        } else {
            ALOGE("DTS_EAGLE_HAL param (get) detected but failed parse: %s", str_parms_to_str(query));
        }
    }

    ALOGV("%s: exit", __func__);
    return 0;
}
#endif /* DTS_EAGLE_ENABLED end */
