#include "cJSON.h"
#include "glance.h"
#include <stdio.h>
#include <string.h>

bool glance_settings_patch(const char *json, size_t length, const glance_registry_t *registry,
                           const glance_settings_t *current, glance_settings_t *updated,
                           char *error, size_t error_size) {
    const char *end = NULL;
    const char *message = "Invalid JSON";
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!cJSON_IsObject(root))
        goto fail;
    while (end < json + length &&
           (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t' || !*end))
        end++;
    if (end != json + length)
        goto fail;
    glance_settings_t candidate = *current;
    cJSON *field;
    cJSON_ArrayForEach(field, root) {
        // Duplicate keys have ambiguous semantics and are rejected.
        for (cJSON *prior = root->child; prior != field; prior = prior->next)
            if (!strcmp(prior->string, field->string)) {
                message = "Duplicate field";
                goto fail;
            }
        if (!strcmp(field->string, "brightness")) {
            message = "brightness must be an integer from 0 to 255";
            if (!cJSON_IsNumber(field) || field->valuedouble < 0 || field->valuedouble > 255 ||
                field->valuedouble != (int)field->valuedouble)
                goto fail;
            candidate.brightness = (uint8_t)field->valueint;
        } else if (!strcmp(field->string, "slide_seconds")) {
            message = "slide_seconds must be an integer from 5 to 60";
            if (!cJSON_IsNumber(field) || field->valuedouble < 5 || field->valuedouble > 60 ||
                field->valuedouble != (int)field->valuedouble)
                goto fail;
            candidate.slide_seconds = (uint8_t)field->valueint;
        } else if (!strcmp(field->string, "night_dim")) {
            message = "night_dim must be a boolean";
            if (!cJSON_IsBool(field))
                goto fail;
            candidate.night_dim = cJSON_IsTrue(field);
        } else if (!strcmp(field->string, "enabled")) {
            message = "enabled must contain known feature IDs, with at least one enabled";
            if (!cJSON_IsArray(field) || !cJSON_GetArraySize(field) ||
                cJSON_GetArraySize(field) > (int)registry->count)
                goto fail;
            memset(candidate.enabled, 0, sizeof(candidate.enabled));
            cJSON *item;
            cJSON_ArrayForEach(item, field) {
                if (!cJSON_IsString(item))
                    goto fail;
                size_t i = 0;
                for (; i < registry->count; i++)
                    if (!strcmp(item->valuestring, registry->features[i]->id))
                        break;
                if (i == registry->count || candidate.enabled[i])
                    goto fail;
                candidate.enabled[i] = true;
            }
        } else {
            message = "Unknown settings field";
            goto fail;
        }
    }
    *updated = candidate;
    cJSON_Delete(root);
    if (error_size)
        error[0] = 0;
    return true;
fail:
    cJSON_Delete(root);
    if (error_size)
        snprintf(error, error_size, "%s", message);
    return false;
}
