# NodeMCU / ESP32 Web Panel

Веб-панель с вкладками, которая работает прямо на плате. Плата сама раздаёт Wi-Fi, вы подключаетесь с телефона и открываете страницу в браузере. Внешние библиотеки не нужны.

| Папка | Плата | Ядро Arduino |
|---|---|---|
| `nodemcu_web_panel` | NodeMCU v3 (ESP8266, CH340G) | esp8266 3.1.x |
| `esp32_web_panel` | ESP32 DevKit | esp32 3.x |

## Вкладки

- **Главная**: аптайм, свободная память, IP-адреса, уровень сигнала.
- **Игра** (только NodeMCU): аркада «Звездопад». Таблица рекордов хранится на плате, при новом рекорде мигает светодиод.
- **Управление**: встроенный светодиод, включение и яркость.
- **Wi-Fi**: поиск сетей и подключение к домашнему роутеру.
- **Настройки**: имя устройства, имя и пароль точки доступа. Хранятся в памяти платы.
- **Система**: информация о чипе, перезагрузка, сброс настроек.

## Подключение с телефона

1. Подключитесь к Wi-Fi `NodeMCU-Panel` (или `ESP32-Panel`), пароль `12345678`.
2. Откройте http://192.168.4.1.

После подключения к роутеру панель доступна и по адресу `http://nodemcu.local` (или `http://esp32.local`).

## Прошивка через arduino-cli

```bash
arduino-cli config add board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
arduino-cli core install esp8266:esp8266
arduino-cli compile --upload -p COM4 --fqbn esp8266:esp8266:nodemcuv2 nodemcu_web_panel
```

Для ESP32: ядро `esp32:esp32` (индекс `https://espressif.github.io/arduino-esp32/package_esp32_index.json`), плата `esp32:esp32:esp32`.

В Arduino IDE выберите плату **NodeMCU 1.0 (ESP-12E Module)** или **ESP32 Dev Module** и нажмите «Загрузить».

## Если порт не открывается (CH340)

Ошибка `PermissionError(13, ..., 31)` / «A device attached to the system is not functioning» на платах с клоном CH340 появляется из-за новых драйверов WCH (3.8 и новее). Помогает драйвер **3.5.2019.1** из каталога Microsoft Update. Команды выполнять в PowerShell от администратора:

```bash
pnputil /delete-driver oem8.inf /uninstall /force
pnputil /add-driver "путь\к\CH341SER.INF" /install
```

Имя `oem8.inf` может отличаться, его можно посмотреть командой `pnputil /enum-drivers /class Ports`.
