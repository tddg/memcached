/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <algorithm>
#include <chrono>
#include <queue>
#include <map>
#include <sstream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cJSON.h>
#include "auditd.h"
#include "audit.h"
#include "eventdata.h"

EXTENSION_LOGGER_DESCRIPTOR* Audit::logger = NULL;
std::string Audit::hostname;


void Audit::log_error(const ErrorCode return_code, const char *string) {
    switch (return_code) {
        case AUDIT_EXTENSION_DATA_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "audit extension data error");
            break;
        case FILE_ATTRIBUTES_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                               "attributes error on file %s: %s", string, strerror(errno));
            break;
        case FILE_OPEN_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                               "open error on file %s: %s", string, strerror(errno));
            break;
        case FILE_RENAME_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                               "rename error on file %s: %s", string, strerror(errno));
            break;
        case FILE_REMOVE_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                               "remove error on file %s: %s", string, strerror(errno));
            break;
        case MEMORY_ALLOCATION_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL,
                               "memory allocation error: %s", string);
            break;
        case JSON_PARSING_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON parsing error on string "
                        "\"%s\"", string);
            break;
        case JSON_MISSING_DATA_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON missing data error");
            break;
        case JSON_MISSING_OBJECT_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON missing object error");
            break;
        case JSON_KEY_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON key \"%s\" error", string);
            break;
        case JSON_ID_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON eventid error");
            break;
        case JSON_UNKNOWN_FIELD_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "JSON unknown field error");
            break;
        case CB_CREATE_THREAD_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "cb create thread error");
            break;
        case EVENT_PROCESSING_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "event processing error");
            break;
        case PROCESSING_EVENT_FIELDS_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "processing events field error");
            break;
        case TIMESTAMP_MISSING_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "timestamp missing error");
            break;
        case TIMESTAMP_FORMAT_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL, "timestamp format error on string \"%s\"", string);
            break;
        case EVENT_ID_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "eventid error");
            break;
        case VERSION_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "audit version error");
            break;
        case VALIDATE_PATH_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL, "validate path \"%s\" error", string);
            break;
        case ROTATE_INTERVAL_BELOW_MIN_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "rotate_interval below minimum error");
            break;
        case ROTATE_INTERVAL_EXCEEDS_MAX_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "rotate_interval exceeds maximum error");
            break;
        case DROPPING_EVENT_ERROR:
            logger->log(EXTENSION_LOG_WARNING, NULL, "error: dropping event with payload = %s",
                        string);
            break;
        case SETTING_AUDITFILE_OPEN_TIME_ERROR:
            assert(string != NULL);
            logger->log(EXTENSION_LOG_WARNING, NULL, "error: setting auditfile open time = %s",
                        string);
            break;
        default:
            assert(false);
    }
}


std::string Audit::load_file(const char *file) {
    std::ifstream myfile(file, std::ios::in | std::ios::binary);
    if (myfile.is_open()) {
        std::string str((std::istreambuf_iterator<char>(myfile)),
                        std::istreambuf_iterator<char>());
        myfile.close();
        return str;
    } else {
        Audit::log_error(FILE_OPEN_ERROR, file);
        std::string str;
        return str;
    }
}


bool Audit::is_timestamp_format_correct (std::string& str) {
    const char *data = str.c_str();
    if (str.length() < 19) {
        return false;
    } else if (isdigit(data[0]) && isdigit(data[1]) &&
        isdigit(data[2]) && isdigit(data[3]) &&
        data[4] == '-' &&
        isdigit(data[5]) && isdigit(data[6]) &&
        data[7] == '-' &&
        isdigit(data[8]) && isdigit(data[9]) &&
        data[10] == 'T' &&
        isdigit(data[11]) && isdigit(data[12]) &&
        data[13] == ':' &&
        isdigit(data[14]) && isdigit(data[15]) &&
        data[16] == ':' &&
        isdigit(data[17]) && isdigit(data[18])) {
        return true;
    }
    return false;
}


std::string Audit::generatetimestamp(void) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::chrono::system_clock::duration seconds_since_epoch =
    std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    time_t now_t(std::chrono::system_clock::to_time_t(
                 std::chrono::system_clock::time_point(seconds_since_epoch)));
    std::chrono::milliseconds frac_of_second (
    std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch() - seconds_since_epoch));
    struct tm *utc_time = gmtime(&now_t) ;
    struct tm *local_time = localtime(&now_t);
    time_t utc = mktime(utc_time);
    time_t local = mktime(local_time);
    double total_seconds_diff = difftime(local, utc);
    double total_minutes_diff = total_seconds_diff / 60;
    int32_t hours = (int32_t)(total_minutes_diff / 60);
    int32_t minutes = (int32_t)(total_minutes_diff) % 60;

    std::stringstream timestamp;
    timestamp << std::setw(4) << std::setfill('0') <<
    local_time->tm_year + 1900 << "-" <<
    std::setw(2) << std::setfill('0') << local_time->tm_mon+1 << "-" <<
    std::setw(2) << std::setfill('0') << local_time->tm_mday << "T" <<
    std::setw(2) << std::setfill('0') << local_time->tm_hour << ":" <<
    std::setw(2) << std::setfill('0') << local_time->tm_min << ":" <<
    std::setw(2) << std::setfill('0') << local_time->tm_sec << "." <<
    std::setw(3) << std::setfill('0') << std::setprecision(3) <<
    frac_of_second.count();

    if (total_seconds_diff == 0.0) {
        timestamp << "Z";
    } else if (total_seconds_diff < 0.0) {
        timestamp << "-" <<
        std::setw(2) << std::setfill('0') << abs(hours) <<
        std::setw(2) << std::setfill('0') << abs(minutes);
    } else {
        timestamp << "+" <<
        std::setw(2) << std::setfill('0') << hours <<
        std::setw(2) << std::setfill('0') << minutes;
    }
    return timestamp.str();
}


bool Audit::create_audit_event(uint32_t event_id, cJSON *payload) {
    switch (event_id) {
        case 0x1000:
        case 0x1002: {
            cJSON_AddStringToObject(payload, "timestamp", generatetimestamp().c_str());
            cJSON_AddStringToObject(payload, "archive_path", config.archive_path.c_str());
            if (config.auditd_enabled) {
                cJSON_AddTrueToObject(payload, "auditd_enabled");
            } else {
                cJSON_AddFalseToObject(payload, "auditd_enabled");
            }
            cJSON_AddStringToObject(payload, "log_path", config.log_path.c_str());
            cJSON *real_userid = cJSON_CreateObject();
            cJSON_AddStringToObject(real_userid, "source", "internal");
            cJSON_AddStringToObject(real_userid, "user", "couchbase");
            cJSON_AddItemReferenceToObject(payload, "real_userid", real_userid);
            cJSON_AddNumberToObject(payload, "rotate_interval", config.rotate_interval);
            cJSON_AddNumberToObject(payload, "version", 1.0);
            break;
        }
        case 0x1001:
        case 0x1003:
        case 0x1004: {
            cJSON_AddStringToObject(payload, "timestamp", generatetimestamp().c_str());
            cJSON *real_userid = cJSON_CreateObject();
            cJSON_AddStringToObject(real_userid, "source", "internal");
            cJSON_AddStringToObject(real_userid, "user", "couchbase");
            cJSON_AddItemReferenceToObject(payload, "real_userid", real_userid);
            break;
        }
        default:
            log_error(EVENT_ID_ERROR, NULL);
            return false;
    }
    return true;
}


bool Audit::initialize_event_data_structures(cJSON *event_ptr) {
    if (event_ptr == NULL) {
        log_error(JSON_MISSING_DATA_ERROR, NULL);
        return false;
    }
    uint32_t eventid;
    bool set_eventid = false;
    EventData* eventdata;
    cJSON* values_ptr = event_ptr->child;
    if (values_ptr == NULL) {
        log_error(JSON_MISSING_DATA_ERROR, NULL);
        return false;
    }
    try {
        eventdata = new EventData;
    } catch (std::bad_alloc& ba) {
        log_error(MEMORY_ALLOCATION_ERROR, ba.what());
        return false;
    }
    while (values_ptr != NULL) {
        switch (values_ptr->type) {
            case cJSON_Number:
                if (strcmp(values_ptr->string, "id") == 0) {
                    eventid = values_ptr->valueint;
                    assert(eventid != 0);
                    set_eventid = true;
                } else {
                    log_error(JSON_KEY_ERROR,values_ptr->string);
                    return false;
                }
                break;
            case cJSON_String:
                if (strcmp(values_ptr->string, "name") == 0) {
                    eventdata->name = std::string(values_ptr->valuestring);
                } else if (strcmp(values_ptr->string, "description") == 0) {
                    eventdata->description = std::string(values_ptr->valuestring);
                } else {
                    log_error(JSON_KEY_ERROR, event_ptr->string);
                    return false;
                }
                break;
            case cJSON_True:
            case cJSON_False:
                if ((strcmp(values_ptr->string, "sync") != 0) &&
                    (strcmp(values_ptr->string, "enabled") != 0)) {
                    log_error(JSON_KEY_ERROR,values_ptr->string);
                    return false;
                }
                break;
            case cJSON_Object:
            case cJSON_Array:
                break;
            default:
                log_error(JSON_UNKNOWN_FIELD_ERROR, NULL);
                return false;
        }
        values_ptr = values_ptr->next;
    }
    if (set_eventid) {
        eventdata->sync = (std::find(config.sync.begin(),
                                     config.sync.end(), eventid) !=
                           config.sync.end()) ? true : false;
        eventdata->enabled = (std::find(config.enabled.begin(),
                                        config.enabled.end(), eventid)
                              != config.enabled.end()) ? true : false;
        events.insert(std::pair<uint32_t, EventData*>(eventid, eventdata));
    } else {
        Audit::log_error(JSON_ID_ERROR, NULL);
        return false;
    }
    return true;
}


bool Audit::process_module_data_structures(cJSON *module) {
    if (module == NULL) {
        log_error(JSON_MISSING_OBJECT_ERROR, NULL);
        return false;
    }
    while (module != NULL) {
        cJSON *mod_ptr = module->child;
        if (mod_ptr == NULL) {
            log_error(JSON_MISSING_DATA_ERROR, NULL);
            return false;
        }
        while (mod_ptr != NULL) {
            cJSON *event_ptr;
            switch (mod_ptr->type) {
                case cJSON_Number:
                case cJSON_String:
                    break;
                case cJSON_Array:
                    event_ptr = mod_ptr->child;
                    while (event_ptr != NULL) {
                        if (!initialize_event_data_structures(event_ptr)) {
                            return false;
                        }
                        event_ptr = event_ptr->next;
                    }
                    break;
                default:
                    log_error(JSON_UNKNOWN_FIELD_ERROR, NULL);
                    return false;
            }
            mod_ptr = mod_ptr->next;
        }
        module = module->next;
    }
    return true;
}


bool Audit::process_module_descriptor(cJSON *module_descriptor) {
    while(module_descriptor != NULL) {
        switch (module_descriptor->type) {
            case cJSON_Number:
                break;
            case cJSON_Array:
                if (!process_module_data_structures(module_descriptor->child)) {
                    return false;
                }
                break;
            default:
                log_error(JSON_UNKNOWN_FIELD_ERROR, NULL);
                return false;
        }
        module_descriptor = module_descriptor->next;
    }
    return true;
}


bool Audit::process_event(Event& event) {
    // convert the event.payload into JSON
    cJSON *json_payload = cJSON_Parse(event.payload.c_str());
    if (json_payload == NULL) {
        log_error(JSON_PARSING_ERROR, event.payload.c_str());
        return false;
    }
    std::map<std::string, cJSON *> fields_map;
    cJSON *fields = json_payload->child;
    while (fields != NULL) {
        std::string name = fields->string;
        fields_map[name] = fields;
        fields = fields->next;
    }
    if (!auditfile.open_time_set) {
        if (!auditfile.set_auditfile_open_time(
                        std::string(fields_map["timestamp"]->valuestring))) {
            log_error(SETTING_AUDITFILE_OPEN_TIME_ERROR,
                      fields_map["timestamp"]->valuestring);
        }
    }

    // create stringstream of event
    std::stringstream output;
    output << "{\"timestamp\":\"" << fields_map["timestamp"]->valuestring << "\", ";
    output << "\"id\":" << event.id << ", ";

    // write out the name & description
    output << "\"name\":\"" << events[event.id]->name << "\", ";
    output << "\"desc\":\"" << events[event.id]->description << "\"";

    // remove timestamp from json_payload
    cJSON_DeleteItemFromObject(json_payload, "timestamp");
    char *content = cJSON_PrintUnformatted(json_payload);
    std::string mystring = content;
    mystring.replace(0,1,",");
    size_t start_pos = 0;
    while((start_pos = mystring.find(",", start_pos)) != std::string::npos) {
        mystring.replace(start_pos, 1, ", ");
        start_pos += 2;
    }
    output << mystring << std::endl;
    auditfile.write_event_to_disk(output);
    return true;
}


bool Audit::add_to_filleventqueue(uint32_t event_id,
                                  const char *payload,
                                  size_t length) {
    // @todo we might need to protect the access to the map if we'd
    //       like to be able to modifications to the map itself.
    auto evt = events.find(event_id);
    if (evt == events.end()) {
        // This is an unknown id. drop it!
        // We should change this to return false at some point because
        // people should know that they're sending an unknown identifier
        return true;
    }
    // protect access to "enabled" because reload config can change the value
    cb_mutex_enter(&producer_consumer_lock);
    bool enabled = evt->second->enabled;
    cb_mutex_exit(&producer_consumer_lock);
    if (enabled) {
        // @todo I think we should do full validation of the content
        //       in debug mode to ensure that developers actually fill
        //       in the correct fields.. if not we should add an
        //       event to the audit trail saying it is one in an illegal
        //       format (or missing fields)
        Event new_event;
        new_event.id = event_id;
        new_event.payload.assign(payload, length);
        assert(filleventqueue != NULL);
        filleventqueue->push(new_event);
        cb_mutex_enter(&producer_consumer_lock);
        cb_cond_broadcast(&events_arrived);
        cb_mutex_exit(&producer_consumer_lock);
    }
    return true;
}


void Audit::clear_events_map(void) {
    typedef std::map<uint32_t, EventData*>::iterator it_type;
    for(it_type iterator = events.begin(); iterator != events.end(); iterator++) {
        assert(iterator->second != NULL);
        delete iterator->second;
    }
    events.clear();
}


void Audit::clear_events_queues(void) {
    while(!eventqueue1.empty()) {
        eventqueue1.pop();
    }
    while(!eventqueue2.empty()) {
        eventqueue2.pop();
    }
}


void Audit::clean_up(void) {
    clear_events_map();
    clear_events_queues();
}
