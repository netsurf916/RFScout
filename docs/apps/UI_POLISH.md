# Interface polish: GPS, OTA, and station privacy

The GPS status indicator uses a compact satellite silhouette instead of a dish.
Its color preserves the shared status bar convention: warning without a fresh
position fix, success with a fresh position fix. A missing GPS receiver has
no icon.

The Update page reads the connected SSID from the shared network session and
copies it into persistent menu-owned storage before rendering. The Network
settings screen continues to use the same saved/scan list as before.

Infrastructure Wi-Fi connections initiated by the shared network session
already request a newly generated locally administered unicast station MAC on
each connection attempt (including reconnecting to the same saved SSID).
If the driver cannot generate and apply that randomized MAC, it now returns an
error instead of silently connecting using the factory/current address.
This policy does not promise randomized MACs for passive scans or unrelated
Wi-Fi roles, and the MAC stays constant for the duration of one connection.

Hardware validation: repeatedly connect to one SSID and confirm distinct
station MAC addresses on the AP, confirm OTA shows the full network name, and
verify GPS icon states before and after a location fix.
