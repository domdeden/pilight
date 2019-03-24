/*
    Copyright (C) 2014 CurlyMo

    This file is part of pilight.

    pilight is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version.

    pilight is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
    A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with pilight. If not, see    <http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "tfa2.h"

#define PULSE_MULTIPLIER    6
#define MIN_PULSE_LENGTH    460
#define MAX_PULSE_LENGTH    530
#define AVG_PULSE_LENGTH    471
#define RAW_LENGTH                82

typedef struct settings_t {
    double id;
    double channel;
    double temp;
    double humi;
    struct settings_t *next;
} settings_t;

static struct settings_t *settings = NULL;

static int validate(void) {
    if(tfa2->rawlen == RAW_LENGTH) {
        if(tfa2->raw[tfa2->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
           tfa2->raw[tfa2->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
            return 0;
        }
    }

    return -1;
}

static void parseCode(void) {
    int binary[RAW_LENGTH/2];
    int temp1 = 0;//, temp2 = 0, temp3 = 0;
    int humi1 = 0, humi2 = 0;
    int id = 0, battery = 0;
    int channel = 0;
    int i = 0, x = 0;
    double humi_offset = 0.0, temp_offset = 0.0;
    double temperature = 0.0, humidity = 0.0;
    char binstr[RAW_LENGTH/2+1];

    for(x=1;x<tfa2->rawlen-2;x+=2) {
        if(tfa2->raw[x] > AVG_PULSE_LENGTH*PULSE_MULTIPLIER) {
            binstr[i]='1';
            binary[i++] = 1;
        } else {
            binstr[i]='0';
            binary[i++] = 0;
        }
    }
    binstr[i]='\0';

    id = binToDecRev(binary, 0, 7);
    channel = binToDecRev(binary, 38, 39);

    temp1 = binToDecRev(binary, 16, 27);
                                                         /* Convert F to C */
    temperature = (int)((float)(((temp1*10) - 9000) - 3200) * ((float)5/(float)9));

    humi1 = binToDecRev(binary, 28, 31);
    humi2 = binToDecRev(binary, 32, 35);
    humidity = ((humi1*10)+(humi2));

    if(binToDecRev(binary, 36, 37) > 1) {
        battery = 0;
    } else {
        battery = 1;
    }

    struct settings_t *tmp = settings;
    while(tmp) {
        if(fabs(tmp->id-id) < EPSILON && fabs(tmp->channel-channel) < EPSILON) {
            humi_offset = tmp->humi;
            temp_offset = tmp->temp;
            break;
        }
        tmp = tmp->next;
    }

    temperature += temp_offset;
    humidity += humi_offset;

    tfa2->message = json_mkobject();
    json_append_member(tfa2->message, "id", json_mknumber(id, 0));
    json_append_member(tfa2->message, "temperature", json_mknumber(temperature/100, 2));
    json_append_member(tfa2->message, "humidity", json_mknumber(humidity, 2));
    json_append_member(tfa2->message, "battery", json_mknumber(battery, 0));
    json_append_member(tfa2->message, "channel", json_mknumber(channel, 0));
    json_append_member(tfa2->message, "binary", json_mkstring(binstr));
}

static int checkValues(struct JsonNode *jvalues) {
    struct JsonNode *jid = NULL;

    if((jid = json_find_member(jvalues, "id"))) {
        struct settings_t *snode = NULL;
        struct JsonNode *jchild = NULL;
        struct JsonNode *jchild1 = NULL;
        double channel = -1, id = -1;
        int match = 0;

        jchild = json_first_child(jid);
        while(jchild) {
            jchild1 = json_first_child(jchild);
            while(jchild1) {
                if(strcmp(jchild1->key, "channel") == 0) {
                    channel = jchild1->number_;
                }
                if(strcmp(jchild1->key, "id") == 0) {
                    id = jchild1->number_;
                }
                jchild1 = jchild1->next;
            }
            jchild = jchild->next;
        }

        struct settings_t *tmp = settings;
        while(tmp) {
            if(fabs(tmp->id-id) < EPSILON && fabs(tmp->channel-channel) < EPSILON) {
                match = 1;
                break;
            }
            tmp = tmp->next;
        }

        if(match == 0) {
            if((snode = MALLOC(sizeof(struct settings_t))) == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(EXIT_FAILURE);
            }
            snode->id = id;
            snode->channel = channel;
            snode->temp = 0;
            snode->humi = 0;

            json_find_number(jvalues, "temperature-offset", &snode->temp);
            json_find_number(jvalues, "humidity-offset", &snode->humi);

            snode->next = settings;
            settings = snode;
        }
    }
    return 0;
}

static void gc(void) {
    struct settings_t *tmp = NULL;
    while(settings) {
        tmp = settings;
        settings = settings->next;
        FREE(tmp);
    }
    if(settings != NULL) {
        FREE(settings);
    }
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void tfa2Init(void) {
    protocol_register(&tfa2);
    protocol_set_id(tfa2, "tfa2");
    protocol_device_add(tfa2, "tfa2", "inFactory weather sensor");
    tfa2->devtype = WEATHER;
    tfa2->hwtype = RF433;
    tfa2->maxgaplen = MAX_PULSE_LENGTH*PULSE_DIV;
    tfa2->mingaplen = MIN_PULSE_LENGTH*PULSE_DIV;
    tfa2->minrawlen = 82;
    tfa2->maxrawlen = 82;
    
    options_add(&tfa2->options, 't', "temperature", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,3}$");
    options_add(&tfa2->options, 'i', "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");
    options_add(&tfa2->options, 'c', "channel", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");
    options_add(&tfa2->options, 'h', "humidity", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "[0-9]");
    options_add(&tfa2->options, 'b', "battery", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[01]$");

    // options_add(&tfa2->options, 0, "decimals", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)2, "[0-9]");
    options_add(&tfa2->options, 0, "temperature-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
    options_add(&tfa2->options, 0, "humidity-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
    options_add(&tfa2->options, 0, "temperature-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)2, "[0-9]");
    options_add(&tfa2->options, 0, "humidity-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)2, "[0-9]");
    options_add(&tfa2->options, 0, "show-humidity", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
    options_add(&tfa2->options, 0, "show-temperature", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
    options_add(&tfa2->options, 0, "show-battery", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
    
    tfa2->parseCode=&parseCode;
    tfa2->checkValues=&checkValues;
    tfa2->validate=&validate;
    tfa2->gc=&gc;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
    module->name = "tfa2";
    module->version = "1.0";
    module->reqversion = "6.0";
    module->reqcommit = "84";
}

void init(void) {
    tfa2Init();
}
#endif
