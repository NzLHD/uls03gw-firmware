"""Serve the real setup page with example (fake) settings, for the manual's screenshot."""
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PAGE = open(sys.argv[1], 'rb').read()
STATUS = {
  "status": {"fw": "0.2.2", "mac": "10:5a:17:ab:cd:ef", "power": "battery", "rssi_last": -62,
             "distance_mm": 655, "depth_cm": 114, "level_pct": 73, "alarm": "normal", "temp_c": 21,
             "last_send": "ok", "last_error": None, "boot_count": 412},
  "config": {"install_height_cm": 180, "full_distance_cm": 25, "upper_alarm_pct": 90, "lower_alarm_pct": 20,
             "tank_volume_l": 1000, "change_threshold_cm": 2, "read_interval_min": 30, "heartbeat_h": 6,
             "mains_read_s": 10, "mains_heartbeat_min": 30, "sensor_warmup_ms": 500, "sensor_poll_ms": 500,
             "sensor_samples": 1, "ap_timeout_min": 5, "ap_password_set": True, "wifi_ssid": "MyHomeWiFi",
             "wifi_pass_set": True, "hostname": "uls03gw-abcdef", "wifi_static": False, "wifi_ip": "",
             "wifi_mask": "", "wifi_gw": "", "wifi_dns": "", "mqtt_host": "mqtt.home.lan", "mqtt_port": 8883,
             "mqtt_tls": True, "mqtt_ca_pem_set": True, "mqtt_verify_host": True, "mqtt_user": "tank",
             "mqtt_pass_set": True, "mqtt_client_id": "uls03gw-abcdef", "mqtt_base_topic": "uls03gw/abcdef",
             "ha_discovery": True, "ha_prefix": "homeassistant"}}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype):
        self.send_response(200); self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path.startswith('/api/status'): self.send(json.dumps(STATUS).encode(), 'application/json')
        elif self.path.startswith('/api/scan'): self.send(b'{"aps":[]}', 'application/json')
        else: self.send(PAGE, 'text/html; charset=utf-8')

HTTPServer(('127.0.0.1', 8765), H).serve_forever()
