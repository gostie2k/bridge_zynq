# Отчет о перезагрузках Watchdog (SLCR.REBOOT_STATUS)

Данный документ содержит транскрипцию логов сбоев из графического интерфейса управления модемом (PKRV Modem Control).

---

## 📡 MASTER (Изображения 1–5)

### Лог 1 (image_c01d65.png)
* **REBOOT_STATUS:** `0x0008211F` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x0F (MAIN, LWIP, TX, RX)`

> ⚠️ **Примечание:** Не собралось созвездие после этого. Даже после ребута slave. Восстановилось, только после повторного ребута.

---

### Лог 2 (image_c01e26.png)
* **REBOOT_STATUS:** `0x0008311B` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x0B (MAIN, LWIP, RX)`

---

### Лог 3 (image_c01e3e.png)
* **REBOOT_STATUS:** `0x00082713` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `AD9361_SPI`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x03 (MAIN, LWIP)`

---

### Лог 4 (image_c01e43.png)
* **REBOOT_STATUS:** `0x00082717` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `AD9361_SPI`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x07 (MAIN, LWIP, TX)`

---

### Лог 5 (image_c01e5c.png)
* **REBOOT_STATUS:** `0x00082117` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x07 (MAIN, LWIP, TX)`

---

## 📡 SLAVE (Изображения 6–7)

### Лог 6 (image_c01e5f.jpg)
* **REBOOT_STATUS:** `0x0008271F` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `AD9361_SPI`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x0F (MAIN, LWIP, TX, RX)`

---

### Лог 7 (image_c01e64.jpg)
* **REBOOT_STATUS:** `0x0008311F` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `AD9361_SPI`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x0F (MAIN, LWIP, TX, RX)`
