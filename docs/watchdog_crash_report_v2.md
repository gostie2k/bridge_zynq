# Отчет о перезагрузках Watchdog (SLCR.REBOOT_STATUS) — Версия 2

Данный документ содержит транскрипцию новой серии логов сбоев из графического интерфейса управления модемом (PKRV Modem Control).

---

## 📡 MASTER (Изображения 1–4)

### Лог 1 (image_c0a148.png)
* **REBOOT_STATUS:** `0x00082A17` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `?10`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x07 (MAIN, LWIP, TX)`

---

### Лог 2 (image_c0a161.png)
* **REBOOT_STATUS:** `0x00082117` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x07 (MAIN, LWIP, TX)`

---

### Лог 3 (image_c0a166.png)
* **REBOOT_STATUS:** `0x00082113` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x03 (MAIN, LWIP)`

---

### Лог 4 (image_c0a180.png)
* **REBOOT_STATUS:** `0x00082A13` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `?10`
* **Oldest HB при crash'e:** `TX`
* **Starve mask при crash'e:** `0x03 (MAIN, LWIP)`

---

## 📡 SLAVE (Изображения 5–6)

### Лог 5 (image_c0a1be.jpg)
* **REBOOT_STATUS:** `0x0008311F` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x0F (MAIN, LWIP, TX, RX)`

---

### Лог 6 (image_c0a1c1.jpg)
* **REBOOT_STATUS:** `0x0008311B` (WDT_MAGIC, AWDT1_RST)
* **WDT ресет:** ДА — последний ресет был от watchdog
* **Crash section:** `LWIP_INPUT`
* **Oldest HB при crash'e:** `RX`
* **Starve mask при crash'e:** `0x0B (MAIN, LWIP, RX)`
