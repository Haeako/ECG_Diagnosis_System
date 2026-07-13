/* Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"

#define URI "wss://thingsboard.ecgproject.site"
#define PORT 443
#define MQTT_PATH "/mqtt"
#define ACCESS_TOKEN "pwnvut1d1q3z1q0w6ciw"
#define TOPIC "v1/devices/me/telemetry"

extern volatile bool mqtt_is_connected;

#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN
static const char cert_override_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDE "\n"
    "-----END CERTIFICATE-----";
#endif

#if CONFIG_EXAMPLE_CERT_VALIDATE_MOSQUITTO_CA
/* Embedded Mosquitto CA certificate for test.mosquitto.org:8883 */
extern const uint8_t mosquitto_org_crt_start[] asm("_binary_mosquitto_org_crt_start");
extern const uint8_t mosquitto_org_crt_end[] asm("_binary_mosquitto_org_crt_end");

#endif

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void mqtt_init(void);
void publish(const char *topic, const char *data);
void subscribe_single_topic(const char *topic);
void unsubscribe_single_topic(const char *topic);
