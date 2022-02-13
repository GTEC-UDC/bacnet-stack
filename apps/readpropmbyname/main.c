/*************************************************************************
 * Copyright (C) 2008 Steve Karg <skarg@users.sourceforge.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *********************************************************************/

/* command line tool that sends a BACnet service, and displays the reply */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h> /* for time */
#ifdef __STDC_ISO_10646__
#include <locale.h>
#endif

#define PRINT_ENABLED 1

#include "bacnet/bacdef.h"
#include "bacnet/config.h"
#include "bacnet/bactext.h"
#include "bacnet/bacerror.h"
#include "bacnet/iam.h"
#include "bacnet/arf.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/binding/address.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/basic/object/device.h"
#include "bacport.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/whois.h"
#include "bacnet/version.h"
/* some demo stuff needed */
#include "bacnet/rpm.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/dlenv.h"

#include "cJSON.h"
#include "glib.h"
#include "csv.h"

/* buffer used for receive */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* global variables used in this file */
static uint32_t Target_Device_Object_Instance = BACNET_MAX_INSTANCE;
static BACNET_READ_ACCESS_DATA *Read_Access_Data;

static GHashTable* Bacnet_Object_Data_Hash_Table;
static GHashTable* Bacnet_Object_Name_Hash_Table;

/* needed to filter incoming messages */
static uint8_t Request_Invoke_ID = 0;
static BACNET_ADDRESS Target_Address;

/* needed for return value of main application */
static bool Error_Detected = false;


static void MyErrorHandler(BACNET_ADDRESS *src,
    uint8_t invoke_id,
    BACNET_ERROR_CLASS error_class,
    BACNET_ERROR_CODE error_code)
{
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf("BACnet Error: %s: %s\n",
            bactext_error_class_name((int)error_class),
            bactext_error_code_name((int)error_code));
        Error_Detected = true;
    }
}

static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    (void)server;
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf(
            "BACnet Abort: %s\n", bactext_abort_reason_name((int)abort_reason));
        Error_Detected = true;
    }
}

static void MyRejectHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    /* FIXME: verify src and invoke id */
    if (address_match(&Target_Address, src) &&
        (invoke_id == Request_Invoke_ID)) {
        printf("BACnet Reject: %s\n",
            bactext_reject_reason_name((int)reject_reason));
        Error_Detected = true;
    }
}

static cJSON *bacapp_json_value(BACNET_OBJECT_PROPERTY_VALUE *object_value)
{
    BACNET_APPLICATION_DATA_VALUE *value = NULL;
    char str_val[1024];
    cJSON *json_val;

    if (object_value && object_value->value) {
        value = object_value->value;
        switch (value->tag) {
#if defined(BACAPP_NULL)
            case BACNET_APPLICATION_TAG_NULL:
                json_val = cJSON_CreateNull();
                break;
#endif
#if defined(BACAPP_BOOLEAN)
            case BACNET_APPLICATION_TAG_BOOLEAN:
                json_val = (value->type.Boolean)
                    ? cJSON_CreateTrue()
                    : cJSON_CreateFalse();
                break;
#endif
#if defined(BACAPP_UNSIGNED)
            case BACNET_APPLICATION_TAG_UNSIGNED_INT:
                json_val = cJSON_CreateNumber(
                    (unsigned long) value->type.Unsigned_Int);
                break;
#endif
#if defined(BACAPP_SIGNED)
            case BACNET_APPLICATION_TAG_SIGNED_INT:
                json_val = cJSON_CreateNumber((long) value->type.Signed_Int);
                break;
#endif
#if defined(BACAPP_REAL)
            case BACNET_APPLICATION_TAG_REAL:
                json_val = cJSON_CreateNumber((double) value->type.Real);
                break;
#endif
#if defined(BACAPP_DOUBLE)
            case BACNET_APPLICATION_TAG_DOUBLE:
                json_val = cJSON_CreateNumber(value->type.Double);
                break;
#endif
#if defined(BACAPP_ENUMERATED)
            case BACNET_APPLICATION_TAG_ENUMERATED:
                json_val = cJSON_CreateNumber(value->type.Enumerated);
                break;
#endif
            default:
                // Else we create a string object
                bacapp_snprintf_value(str_val, sizeof(str_val), object_value);
                json_val = cJSON_CreateString(str_val);
        }
    }

    return json_val;
}

static void my_rpm_ack_items_to_json(
    BACNET_READ_ACCESS_DATA *rpm_data, cJSON *json)
{
    BACNET_OBJECT_PROPERTY_VALUE object_value; /* for bacapp printing */
    BACNET_PROPERTY_REFERENCE *listOfProperties = NULL;
    BACNET_APPLICATION_DATA_VALUE *value = NULL;
    char json_name[1024];
    bool array_value = false;
    cJSON *json_val;
    cJSON *json_val_array;

    if (rpm_data) {
        listOfProperties = rpm_data->listOfProperties;
        while (listOfProperties) {
            value = listOfProperties->value;
            if (value) {
                unsigned int property = listOfProperties->propertyIdentifier;
                int64_t object_instance = (int64_t) rpm_data->object_instance;
                const char *object_name = (const char *)
                    g_hash_table_lookup(Bacnet_Object_Name_Hash_Table, &object_instance);
                
                if (object_name == NULL) {
                    fprintf(stderr, "Name for object with address %ld not found\n", object_instance);

                    const char *bacnet_type_name = bactext_object_type_name(rpm_data->object_type);
                    if (property < 512) {
                        const char *property_name = bactext_property_name(property);
                        snprintf(json_name, sizeof(json_name), "%s_%lu_%s",
                                bacnet_type_name, object_instance, property_name);
                    } else {
                        snprintf(json_name, sizeof(json_name), "%s_%lu_%u",
                                bacnet_type_name, object_instance, property);
                    }
                } else if (property == PROP_PRESENT_VALUE) {
                    strncpy(json_name, object_name, sizeof(json_name));
                } else if (property < 512) {
                    const char *property_name = bactext_property_name(property);
                    snprintf(json_name, sizeof(json_name), "%s_%s",
                             object_name, property_name);
                } else {
                    snprintf(json_name, sizeof(json_name), "%s_%u",
                             object_name, property);
                }

                BACNET_ARRAY_INDEX propertyArrayIndex =
                    listOfProperties->propertyArrayIndex;
                if (propertyArrayIndex != BACNET_ARRAY_ALL) {
                    char array_index[32];
                    snprintf(array_index, sizeof(array_index), "[%d]",
                             propertyArrayIndex);

                    if ((sizeof(json_name) - strlen(json_name)) > strlen(array_index)) {
                        strcat(json_name, array_index);
                    }
                }

                if (value->next) {
                    json_val_array = cJSON_CreateArray();
                    array_value = true;
                } else {
                    array_value = false;
                }

                object_value.object_type = rpm_data->object_type;
                object_value.object_instance = rpm_data->object_instance;

                while (value) {
                    object_value.object_property =
                        listOfProperties->propertyIdentifier;
                    object_value.array_index =
                        listOfProperties->propertyArrayIndex;
                    object_value.value = value;

                    json_val = bacapp_json_value(&object_value);

                    if (array_value) {
                        cJSON_AddItemToArray(json_val_array, json_val);
                    }

                    value = value->next;
                }

                if (array_value) {
                    cJSON_AddItemToObject(json, json_name, json_val_array);
                } else {
                    cJSON_AddItemToObject(json, json_name, json_val);
                }
            } else {
                /* AccessError */
                fprintf(stderr, "BACnet Error: %s: %s\n",
                    bactext_error_class_name(
                        (int)listOfProperties->error.error_class),
                    bactext_error_code_name(
                        (int)listOfProperties->error.error_code));
            }

            listOfProperties = listOfProperties->next;
        }
    }
}

/** Handler for a ReadPropertyMultiple ACK.
 * @ingroup DSRPM
 * For each read property, print out the ACK'd data,
 * and free the request data items from linked property list.
 *
 * @param service_request [in] The contents of the service request.
 * @param service_len [in] The length of the service_request.
 * @param src [in] BACNET_ADDRESS of the source of the message
 * @param service_data [in] The BACNET_CONFIRMED_SERVICE_DATA information
 *                          decoded from the APDU header of this message.
 */
static void My_Read_Property_Multiple_Ack_Handler(uint8_t *service_request,
    uint16_t service_len,
    BACNET_ADDRESS *src,
    BACNET_CONFIRMED_SERVICE_ACK_DATA *service_data)
{
    int len = 0;
    BACNET_READ_ACCESS_DATA *rpm_data;
    BACNET_READ_ACCESS_DATA *old_rpm_data;
    BACNET_PROPERTY_REFERENCE *rpm_property;
    BACNET_PROPERTY_REFERENCE *old_rpm_property;
    BACNET_APPLICATION_DATA_VALUE *value;
    BACNET_APPLICATION_DATA_VALUE *old_value;

    if (address_match(&Target_Address, src) &&
        (service_data->invoke_id == Request_Invoke_ID)) {
        rpm_data = calloc(1, sizeof(BACNET_READ_ACCESS_DATA));
        if (rpm_data) {
            len = rpm_ack_decode_service_request(
                service_request, service_len, rpm_data);
        }
        
        if (len > 0) {
            cJSON *json = cJSON_CreateObject();

            while (rpm_data) {
                my_rpm_ack_items_to_json(rpm_data, json);
                rpm_property = rpm_data->listOfProperties;
                while (rpm_property) {
                    value = rpm_property->value;
                    while (value) {
                        old_value = value;
                        value = value->next;
                        free(old_value);
                    }
                    old_rpm_property = rpm_property;
                    rpm_property = rpm_property->next;
                    free(old_rpm_property);
                }
                old_rpm_data = rpm_data;
                rpm_data = rpm_data->next;
                free(old_rpm_data);
            }

            // print json
            char *json_string = cJSON_Print(json);
            if (json_string == NULL) {
                fprintf(stderr, "Failed to print JSON.\n");
            } else {
                fprintf(stdout, "%s", json_string);
                free(json_string);
            }
            cJSON_Delete(json);
        } else {
            fprintf(stderr, "RPM Ack Malformed! Freeing memory...\n");
            while (rpm_data) {
                rpm_property = rpm_data->listOfProperties;
                while (rpm_property) {
                    value = rpm_property->value;
                    while (value) {
                        old_value = value;
                        value = value->next;
                        free(old_value);
                    }
                    old_rpm_property = rpm_property;
                    rpm_property = rpm_property->next;
                    free(old_rpm_property);
                }
                old_rpm_data = rpm_data;
                rpm_data = rpm_data->next;
                free(old_rpm_data);
            }
        }
    }
}

static void Init_Service_Handlers(void)
{
    Device_Init(NULL);
    /* we need to handle who-is
       to support dynamic device binding to us */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    /* handle i-am to support binding to other devices */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_bind);
    /* set the handler for all the services we don't implement
       It is required to send the proper reject message... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* we must implement read property - it's required! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* handle the data coming back from confirmed requests */
    apdu_set_confirmed_ack_handler(SERVICE_CONFIRMED_READ_PROP_MULTIPLE,
        My_Read_Property_Multiple_Ack_Handler);
    /* handle any errors coming back */
    apdu_set_error_handler(SERVICE_CONFIRMED_READ_PROPERTY, MyErrorHandler);
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

static void cleanup(void)
{
    BACNET_READ_ACCESS_DATA *rpm_object;
    BACNET_READ_ACCESS_DATA *old_rpm_object;
    BACNET_PROPERTY_REFERENCE *rpm_property;
    BACNET_PROPERTY_REFERENCE *old_rpm_property;

    rpm_object = Read_Access_Data;
    old_rpm_object = rpm_object;
    while (rpm_object) {
        rpm_property = rpm_object->listOfProperties;
        while (rpm_property) {
            old_rpm_property = rpm_property;
            rpm_property = rpm_property->next;
            free(old_rpm_property);
        }
        old_rpm_object = rpm_object;
        rpm_object = rpm_object->next;
        free(old_rpm_object);
    }
}

static void print_usage(char *filename)
{
    printf("Usage: %s [--dnet N] [--dadr A] [--mac A]\n", filename);
    printf("       --id device-instance --csv object-data-file object-name [object-name ...]\n");
    printf("       [--version][--help]\n");
}

static void print_help(char *filename)
{
    printf("Read the present-value property from one or more objects\n"
           "in a BACnet device and print the value(s). The objects are\n"
           "specified by their names, and the object type and object\n"
           "number required for each object are provided in an external\n"
           "CSV file. The values are returned in JSON format were each field\n"
           "will have the name of the corresponging object\n"
           "\n");
    printf("--mac A\n"
           "Optional BACnet mac address.\n"
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n"
           "\n"
           "--dnet N\n"
           "Optional BACnet network number N for directed requests.\n"
           "Valid range is from 0 to 65535 where 0 is the local connection\n"
           "and 65535 is network broadcast.\n"
           "\n"
           "--dadr A\n"
           "Optional BACnet mac address on the destination BACnet network "
           "number.\n"
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n"
           "\n"
           "--id device-instance\n"
           "BACnet Device Object Instance number that you are\n"
           "trying to communicate to. This number will be used\n"
           "to try and bind with the device using Who-Is and\n"
           "I-Am services. For example, if you were reading\n"
           "Device Object 123, the device-instance would be 123.\n"
           "\n"
           "--csv object-data-file\n"
           "CSV file containing for each object its name, the object type,\n"
           "and the object instance number, these data must be in columns with\n"
           "names \"name\", \"type_value\", and \"address\", respectively.\n"
           "\n"
           "object-name\n"
           "The name of the object. It should be a name included in the\n"
           "data file specified with the --csv option. For each name the\n"
           "application will look into the data file to obtain the\n"
           "corresponding object type and object instance number, necessary\n"
           "to make the request to the BACnet device.\n");
}


#define MAX_BACNET_NAME_LEN 255 // max lenght of a bacnet object name

struct csv_bacnet_object {
    char name[MAX_BACNET_NAME_LEN+1];
    unsigned int type;
    unsigned int address;
};

struct csv_field_indices {
    int name;
    int type_value;
    int address;
};

struct csv_parser_header_data {
    unsigned int field;
    struct csv_field_indices field_indices;
};

static void csv_header_cb1(void *s, size_t len, void *d)
{
    struct csv_parser_header_data *data = (struct csv_parser_header_data *) d;

    if (strcmp(s, "name") == 0)
    {
        data->field_indices.name = data->field;
    }
    else if (strcmp(s, "type_value") == 0)
    {
        data->field_indices.type_value = data->field;
    }
    else if (strcmp(s, "address") == 0)
    {
        data->field_indices.address = data->field;
    }

    data->field++;
}


struct csv_parser_body_data {
    // position in the file
    unsigned int field;
    unsigned int row;

    // indices of desired fields (based on the labels of first row)
    struct csv_field_indices field_indices;

    // data of the current object
    struct csv_bacnet_object current_object;
};


static void csv_body_cb1(void *s, size_t len, void *d)
{
    struct csv_parser_body_data *data = (struct csv_parser_body_data *) d;

    if (data->field == data->field_indices.name)
    {
        strncpy(data->current_object.name, s, MAX_BACNET_NAME_LEN+1);
        data->current_object.name[MAX_BACNET_NAME_LEN] = 0;
    }
    else if (data->field == data->field_indices.type_value)
    {
        data->current_object.type = strtol(s, NULL, 0);
    }
    else if (data->field == data->field_indices.address)
    {
        data->current_object.address = strtol(s, NULL, 0);
    }

    data->field++;
}


static void csv_body_cb2(int c, void *d)
{
    struct csv_parser_body_data *data = (struct csv_parser_body_data *) d;

    // update row and field
    data->row++;
    data->field = 0;

    // add current object to the hash table
    char *key = strndup(data->current_object.name, MAX_BACNET_NAME_LEN);
    struct csv_bacnet_object *value = malloc(sizeof(struct csv_bacnet_object));
    *value = data->current_object;

    #ifdef DEBUG
        fprintf(stderr, "CSV body -- name:    %s\n", data->current_object.name);
        fprintf(stderr, "CSV body -- type:    %d\n", data->current_object.type);
        fprintf(stderr, "CSV body -- address: %d\n", data->current_object.address);
        fprintf(stderr, "\n");
    #endif

    g_hash_table_replace(Bacnet_Object_Data_Hash_Table, key, value);
}


int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = { 0 }; /* address where message came from */
    uint16_t pdu_len = 0;
    unsigned timeout = 100; /* milliseconds */
    unsigned max_apdu = 0;
    time_t elapsed_seconds = 0;
    time_t last_seconds = 0;
    time_t current_seconds = 0;
    time_t timeout_seconds = 0;
    bool found = false;
    uint8_t buffer[MAX_PDU] = { 0 };
    BACNET_READ_ACCESS_DATA *rpm_object;
    BACNET_PROPERTY_REFERENCE *rpm_property;
    long dnet = -1;
    BACNET_MAC_ADDRESS mac = { 0 };
    BACNET_MAC_ADDRESS adr = { 0 };
    BACNET_ADDRESS dest = { 0 };
    bool specific_address = false;
    int argi = 0;
    char *filename = NULL;

    bool object_instance_set = false;
    char *csvfile = NULL;

    struct csv_parser p;
    unsigned char csv_options = CSV_APPEND_NULL;

    FILE *fp;
    uint8_t csv_buffer[4096] = { 0 };
    char *lineptr;
    size_t len;
    ssize_t nread;

    struct csv_bacnet_object *csv_bacnet_object;
    struct csv_parser_header_data csv_parser_header_data;
    struct csv_parser_body_data csv_parser_body_data;


    // Search for --help or --version options
    filename = filename_remove_path(argv[0]);
    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0) {
            print_usage(filename);
            printf("\n");
            print_help(filename);
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) {
            printf("%s %s\n", filename, BACNET_VERSION_TEXT);
            printf("Copyright (C) 2014 by Steve Karg and others.\n"
                   "This is free software; see the source for copying "
                   "conditions.\n"
                   "There is NO warranty; not even for MERCHANTABILITY or\n"
                   "FITNESS FOR A PARTICULAR PURPOSE.\n");
            return 0;
        }
    }

    if (argc < 3) {
        print_usage(filename);
        return 0;
    }

    // Global hash table to store bacnet object data with name as key
    Bacnet_Object_Data_Hash_Table = g_hash_table_new(g_str_hash, g_str_equal);
    if (Bacnet_Object_Data_Hash_Table == NULL) {
        fprintf(stderr, "Failed to initialize hash table\n");
        return 1;
    }

    // Global hash table to store bacnet object name with address as key
    Bacnet_Object_Name_Hash_Table = g_hash_table_new(g_int64_hash, g_int64_equal);
    if (Bacnet_Object_Name_Hash_Table == NULL) {
        fprintf(stderr, "Failed to initialize hash table\n");
        return 1;
    }

    /* decode the command line parameters */
    argi = 1;
    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--dnet") == 0) {
            if (++argi < argc) {
                dnet = strtol(argv[argi], NULL, 0);
                if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) {
                    specific_address = true;
                }
            }
        } else if (strcmp(argv[argi], "--dadr") == 0) {
            if (++argi < argc) {
                if (address_mac_from_ascii(&adr, argv[argi])) {
                    specific_address = true;
                }
            }
        } else if (strcmp(argv[argi], "--mac") == 0) {
            if (++argi < argc) {
                if (address_mac_from_ascii(&mac, argv[argi])) {
                    specific_address = true;
                }
            }
        } else if (strcmp(argv[argi], "--csv") == 0) {
            if (++argi < argc) {
                csvfile = argv[argi];
            }
        } else if (strcmp(argv[argi], "--id") == 0) {
            if (++argi < argc) {
                Target_Device_Object_Instance = strtol(argv[argi], NULL, 0);
                object_instance_set = true;
            }
        }
        argi++;
    }

    if (!(argi < argc)) {
        print_usage(filename);
        return 0;
    }

    if (!object_instance_set) {
        fprintf(stderr, "The device-instance must be specified with --id\n");
        return 1;
    } else if (Target_Device_Object_Instance >= BACNET_MAX_INSTANCE) {
        fprintf(stderr, "device-instance=%u - it must be less than %u\n",
            Target_Device_Object_Instance, BACNET_MAX_INSTANCE);
        return 1;
    }

    if (csvfile == NULL) {
        fprintf(stderr, "The CSV data file must be specified with --csv\n");
        return 1;
    }

    // Parse CSV file
    if (csv_init(&p, csv_options) != 0) {
        fprintf(stderr, "Failed to initialize csv parser\n");
        return 1;
    }

    fp = fopen(csvfile, "rb");
    if (!fp) {
      fprintf(stderr, "Failed to open %s: %s\n", csvfile, strerror(errno));
      return 1;
    }

    // Parse first line of the CSV
    csv_parser_header_data.field = 0;
    csv_parser_header_data.field_indices.name = -1;
    csv_parser_header_data.field_indices.type_value = -1;
    csv_parser_header_data.field_indices.address = -1;

    lineptr = NULL;
    len = 0;
    if ((nread = getline(&lineptr, &len, fp)) == -1) {
        fprintf(stderr, "Error reading file %s: %s\n", csvfile, strerror(errno));
        return 1;
    }

    if (csv_parse(&p, lineptr, nread, csv_header_cb1, NULL, &csv_parser_header_data) != nread) {
        fprintf(stderr, "Error while parsing file: %s\n", csv_strerror(csv_error(&p)));
        return 1;
    }

    free(lineptr);

    // check that all required fields are present in the CSV
    if (csv_parser_header_data.field_indices.name == -1) {
        fprintf(stderr, "CSV must have a name field");
        return 1;
    }

    if (csv_parser_header_data.field_indices.type_value == -1) {
        fprintf(stderr, "CSV must have a type_value field");
        return 1;
    }

    if (csv_parser_header_data.field_indices.address == -1) {
        fprintf(stderr, "CSV must have an address field");
        return 1;
    }


    // Parse rest of the CSV file
    csv_parser_body_data.field = 0;
    csv_parser_body_data.row = 0;
    csv_parser_body_data.field_indices = csv_parser_header_data.field_indices;

    while ((nread = fread(csv_buffer, 1, 4096, fp)) > 0) {
      if (csv_parse(&p, csv_buffer, nread, csv_body_cb1, csv_body_cb2, &csv_parser_body_data) != nread) {
        fprintf(stderr, "Error while parsing file: %s\n", csv_strerror(csv_error(&p)));
      }
    }

    csv_fini(&p, csv_body_cb1, csv_body_cb2, &csv_parser_body_data);


    // Process rest of the parameters (bacnet objects)
    // Note that now argi points to the beggining of the bacnet objects parameters
    atexit(cleanup);
    Read_Access_Data = NULL;
    rpm_object = NULL;
    for (; argi < argc; argi++) {
        csv_bacnet_object = (struct csv_bacnet_object *)
            g_hash_table_lookup(Bacnet_Object_Data_Hash_Table, argv[argi]);

        if (csv_bacnet_object == NULL) {
            fprintf(stderr, "Error: object %s not defined in %s\n", argv[argi], csvfile);
            continue;
        }

        // Add object name to global hash map with address as key
        int64_t *key = (void *) malloc(sizeof(int64_t));
        *key = csv_bacnet_object->address;
        char *value = strdup(argv[argi]);

        g_hash_table_replace(Bacnet_Object_Name_Hash_Table, key, value);

        // check type
        if (csv_bacnet_object->type < 0) {
            fprintf(stderr, "Object %s: type not defined\n", argv[argi]);
            continue;
        }

        if (csv_bacnet_object->type >= MAX_BACNET_OBJECT_TYPE) {
            fprintf(stderr, "Object %s: type=%u - must be less than %u\n",
                argv[argi], csv_bacnet_object->type, MAX_BACNET_OBJECT_TYPE);
            continue;
        }

        // check address
        if (csv_bacnet_object->address < 0) {
            fprintf(stderr, "Object %s: address not defined\n", argv[argi]);
            continue;
        }

        if (csv_bacnet_object->address > BACNET_MAX_INSTANCE) {
            fprintf(stderr, "Object %s: address=%u - it must be less than %u\n",
                argv[argi], csv_bacnet_object->address, BACNET_MAX_INSTANCE + 1);
            continue;
        }

        if (rpm_object == NULL) {
            rpm_object = calloc(1, sizeof(BACNET_READ_ACCESS_DATA));
            Read_Access_Data = rpm_object;
        } else {
            rpm_object->next = calloc(1, sizeof(BACNET_READ_ACCESS_DATA));
            rpm_object = rpm_object->next;
            rpm_object->next = NULL;
        }

        rpm_object->object_type = csv_bacnet_object->type;
        rpm_object->object_instance = csv_bacnet_object->address;

        rpm_property = calloc(1, sizeof(BACNET_PROPERTY_REFERENCE));
        rpm_property->propertyIdentifier = PROP_PRESENT_VALUE;
        rpm_property->propertyArrayIndex = BACNET_ARRAY_ALL;
        rpm_property->next = NULL;

        rpm_object->listOfProperties = rpm_property;
    }

    if (rpm_object == NULL) {
        fprintf(stderr, "No object for performing the request\n");
        return 1;
    }

    address_init();
    if (specific_address) {
        if (adr.len && mac.len) {
            memcpy(&dest.mac[0], &mac.adr[0], mac.len);
            dest.mac_len = mac.len;
            memcpy(&dest.adr[0], &adr.adr[0], adr.len);
            dest.len = adr.len;
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) {
                dest.net = dnet;
            } else {
                dest.net = BACNET_BROADCAST_NETWORK;
            }
        } else if (mac.len) {
            memcpy(&dest.mac[0], &mac.adr[0], mac.len);
            // printf("%d %d %d %d\n", dest.mac[0], dest.mac[1], dest.mac[2], dest.mac[3]);
            dest.mac_len = mac.len;
            dest.len = 0;
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) {
                dest.net = dnet;
            } else {
                dest.net = 0;
            }
        } else {
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) {
                dest.net = dnet;
            } else {
                dest.net = BACNET_BROADCAST_NETWORK;
            }
            dest.mac_len = 0;
            dest.len = 0;
        }
        address_add(Target_Device_Object_Instance, MAX_APDU, &dest);
    }
    
    /* setup my info */
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    Init_Service_Handlers();
    dlenv_init();
#ifdef __STDC_ISO_10646__
    /* Internationalized programs must call setlocale()
     * to initiate a specific language operation.
     * This can be done by calling setlocale() as follows.
     * If your native locale doesn't use UTF-8 encoding
     * you need to replace the empty string with a
     * locale like "en_US.utf8" which is the same as the string
     * used in the enviromental variable "LANG=en_US.UTF-8".
     */
    setlocale(LC_ALL, "en_US.utf8");
#endif
    atexit(datalink_cleanup);
    /* configure the timeout values */
    last_seconds = time(NULL);
    timeout_seconds = (apdu_timeout() / 1000) * apdu_retries();
    /* try to bind with the device */
    found = address_bind_request(
        Target_Device_Object_Instance, &max_apdu, &Target_Address);
    if (!found) {
        Send_WhoIs(
            Target_Device_Object_Instance, Target_Device_Object_Instance);
    }
    /* loop forever */
    for (;;) {
        /* increment timer - exit if timed out */
        current_seconds = time(NULL);

        /* at least one second has passed */
        if (current_seconds != last_seconds) {
            tsm_timer_milliseconds(((current_seconds - last_seconds) * 1000));
        }
        if (Error_Detected)
            break;
        /* wait until the device is bound, or timeout and quit */
        if (!found) {
            found = address_bind_request(
                Target_Device_Object_Instance, &max_apdu, &Target_Address);
        }
        if (found) {
            if (Request_Invoke_ID == 0) {
                Request_Invoke_ID = Send_Read_Property_Multiple_Request(
                    &buffer[0], sizeof(buffer), Target_Device_Object_Instance,
                    Read_Access_Data);
            } else if (tsm_invoke_id_free(Request_Invoke_ID)) {
                break;
            } else if (tsm_invoke_id_failed(Request_Invoke_ID)) {
                fprintf(stderr, "\rError: TSM Timeout!\n");
                tsm_free_invoke_id(Request_Invoke_ID);
                Error_Detected = true;
                /* try again or abort? */
                break;
            }
        } else {
            /* increment timer - exit if timed out */
            elapsed_seconds += (current_seconds - last_seconds);
            if (elapsed_seconds > timeout_seconds) {
                printf("\rError: APDU Timeout!\n");
                Error_Detected = true;
                break;
            }
        }

        /* returns 0 bytes on timeout */
        pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);

        /* process */
        if (pdu_len) {
            npdu_handler(&src, &Rx_Buf[0], pdu_len);
        }

        /* keep track of time for next check */
        last_seconds = current_seconds;
    }

    if (Error_Detected)
        return 1;
    return 0;
}
