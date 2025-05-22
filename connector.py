import requests
import urllib.parse
import paho.mqtt.client as mqtt

# Blynk IoT settings
BLYNK_AUTH_TOKEN = "dX2H1TJxCSLj4MHLumEE7FavA_HHWVB1"
BLYNK_BASE_URL = "https://blynk.cloud/external/api"

# MQTT settings
MQTT_BROKER = "broker.hivemq.com"  # Use your own broker if needed
MQTT_PORT = 1883
MQTT_TOPIC = "test/wokwi/matlab"

var = 1  # Counter

def send_string_data(msg):
    global var

    # Compose the message
    message = f"Time to take Pill: {msg}"
    var += 1

    # Encode for URL
    encoded_message = urllib.parse.quote(message)

    # Send to Blynk virtual pin V0
    vpin_url = f"{BLYNK_BASE_URL}/update?token={BLYNK_AUTH_TOKEN}&V0={encoded_message}"
    vpin_response = requests.get(vpin_url)

    if vpin_response.ok:
        print(f" Sent to V0: {message}")
    else:
        print(" virtualWrite failed:", vpin_response.text)

    # Send Blynk logEvent
    event_url = f"{BLYNK_BASE_URL}/logEvent?token={BLYNK_AUTH_TOKEN}&event=med_time&description={encoded_message}"
    event_response = requests.get(event_url)

    if event_response.ok:
        print(" Notification sent.")
    else:
        print(" Event log failed:", event_response.text)

# MQTT callback
def on_connect(client, userdata, flags, rc):
    print("MQTT connected with code:", rc)
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    message = msg.payload.decode()
    print(f"📩 MQTT Received: {message}")
    if message == "Type - 1" or message == "Type - 2" or message == "Type - 3":
        send_string_data(message)

# Setup MQTT client
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()
