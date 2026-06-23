#!/usr/bin/env bash
# StudyAid v12 — Step 1: Configure Raspberry Pi 5 as WiFi Access Point
#
# IMPORTANT: This uses NetworkManager (nmcli), NOT hostapd/dnsmasq.
# Raspberry Pi OS Bookworm uses NetworkManager by default — do not follow
# hostapd tutorials, they conflict with NetworkManager on Bookworm.
#
# Run once on the Pi as a regular user (sudo is called where needed):
#   chmod +x 01_setup_ap.sh
#   ./01_setup_ap.sh
#
# After running, reboot and verify with: nmcli connection show studyaid-pi

set -e

AP_SSID="studyaid-pi"
AP_PASS="studyaid123"
PI_IP="192.168.4.1"
DHCP_START="192.168.4.10"
DHCP_END="192.168.4.50"

echo "[1/4] Creating AP connection profile..."
# Delete any previous attempt with the same name (safe to run repeatedly)
sudo nmcli connection delete "$AP_SSID" 2>/dev/null || true

sudo nmcli connection add \
    type wifi \
    ifname wlan0 \
    con-name "$AP_SSID" \
    autoconnect yes \
    ssid "$AP_SSID" \
    -- \
    wifi.mode ap \
    wifi.band bg \
    wifi.channel 6 \
    wifi-sec.key-mgmt wpa-psk \
    wifi-sec.psk "$AP_PASS" \
    wifi-sec.proto rsn \
    wifi-sec.pairwise ccmp \
    wifi-sec.group ccmp \
    wifi-sec.pmf 1 \
    ipv4.method shared \
    ipv4.addresses "$PI_IP/24" \
    ipv4.gateway "" \
    ipv6.method disabled
# IMPORTANT (ESP32-S3 compatibility): the four wifi-sec lines above force WPA2.
#   proto rsn     -> WPA2 (RSN), NOT WPA1. NetworkManager otherwise defaults to
#                    advertising WPA_PSK (WPA1), which the ESP32-S3 Arduino WiFi
#                    stack REFUSES to associate with (phones/laptops tolerate it,
#                    the ESP32 does not — it just silently fails, AP logs nothing).
#   pairwise ccmp -> AES cipher (WPA2 standard)
#   group ccmp    -> AES group cipher
#   pmf 1         -> Protected Management Frames disabled (1=disable in nmcli);
#                    older ESP32 stacks can choke on PMF being advertised.
# Without these the device scan shows "studyaid-pi ... WPA_PSK" and connection
# fails. With them it shows "WPA2_PSK" and the ESP32 connects.

echo "[2/4] Setting DHCP range for device clients..."
# NetworkManager's built-in dnsmasq handles DHCP when ipv4.method=shared.
# We configure its range via a dnsmasq conf drop-in.
sudo mkdir -p /etc/NetworkManager/dnsmasq-shared.d
sudo tee /etc/NetworkManager/dnsmasq-shared.d/studyaid-range.conf > /dev/null <<EOF
dhcp-range=$DHCP_START,$DHCP_END,12h
EOF

echo "[3/4] Reloading NetworkManager..."
sudo systemctl reload NetworkManager
sleep 2

echo "[4/4] Bringing up AP..."
sudo nmcli connection up "$AP_SSID"

echo ""
echo "Done. Access point '$AP_SSID' should now be active."
echo "Pi IP on AP: $PI_IP"
echo ""
echo "Verify with:"
echo "  nmcli connection show studyaid-pi"
echo "  nmcli device status"
echo ""
echo "Confirm WPA2 (critical for ESP32-S3) — should show rsn / ccmp / ccmp:"
echo "  nmcli connection show studyaid-pi | grep -iE 'proto|pairwise|group'"
echo ""
echo "On the device WiFi scan, studyaid-pi must show WPA2_PSK (NOT WPA_PSK)."
echo ""
echo "The ESP32 device should connect to SSID '$AP_SSID'"
echo "and reach the Flask server at http://$PI_IP:5000"
