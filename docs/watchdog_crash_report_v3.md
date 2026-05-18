Данный документ содержит транскрипцию третьей серии логов сбоев из графического интерфейса управления модемом (PKRV Modem Control).

---

## 📡 MASTER (Изображения 1–2)

### Лог 1 (image_c18e03.png)
* **REBOOT_STATUS:** `0x00082117` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x07 (MAIN, LWIP, TX)`

---

### Лог 2 (image_c18e1e.png)
* **REBOOT_STATUS:** `0x00082A1F` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `AD9361_TEMP`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x0F (MAIN, LWIP, TX, RX)`

Лог 3 был SPI_RSSI не успел зафиксировать
---

## 📡 SLAVE (Изображения 3–4)

### Лог 3 (image_c190e7.jpg)
* **REBOOT_STATUS:** `0x00083A1B` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `?10`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x0B (MAIN, LWIP, RX)`

---

### Лог 4 (image_c190ea.jpg)
* **REBOOT_STATUS:** `0x00083C13` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `?12`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x03 (MAIN, LWIP)
