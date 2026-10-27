# Инструкция по настройке новой машины (WSL + VSCode) для ESP32/ESP32-C6

Эта инструкция воспроизводит среду, которая сейчас настроена.

## Требования
- Windows 11/10 с WSL2 (Ubuntu/Debian)
- VSCode с Remote-WSL
- [usbipd-win](https://github.com/dorssel/usbipd-win) на Windows (для проброса USB ESP)
- Доступ к интернету для загрузки ESP-IDF и тулчейнов

## 1. WSL: базовые пакеты
В WSL:
```bash
sudo apt update && sudo apt install -y \
  git wget curl unzip xz-utils zip \
  python3 python3-pip python3-venv \
  build-essential pkg-config \
  libusb-1.0-0 libusb-1.0-0-dev \
  libffi-dev libssl-dev \
  cmake ninja-build flex bison gperf ccache dfu-util
```
> Если python3.14 — убедиться, что есть `python3-venv`. При проблемах с `ensurepip` можно использовать `virtualenv` из pip.

## 2. Установка ESP-IDF v6.1
```bash
mkdir -p ~/.esp
cd ~/.esp
git clone --depth 1 -b v6.1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git esp-idf
cd esp-idf
./install.sh esp32c6
```

Активировать:
```bash
. ./export.sh
idf.py --version  # должно быть ESP-IDF v6.1
```

Добавить алиас в `~/.zshrc` (или `~/.bashrc`):
```bash
echo "alias get_idf='. ~/.esp/esp-idf/export.sh'" >> ~/.zshrc
echo "export IDF_PATH=~/.esp/esp-idf" >> ~/.zshrc
source ~/.zshrc
```

## 3. Проброс USB ESP через usbipd-win (Windows)
На Windows (PowerShell от админа):
```powershell
# Установить usbipd-win (если не установлен) - через winget/msi
winget install --interactive --exact dorssel.usbipd-win

# Найти устройство (VID:PID 303a:1001)
usbipd list

# Поделиться и прокинуть в WSL
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

В WSL проверить: `ls -la /dev/ttyACM*` или `lsusb | grep 303a`

## 4. VSCode
Установить расширения:
- [Espressif IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
- [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools)
- [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)

Создать проект или скопировать существующий. Для ESP-IDF проекта добавить `.vscode/settings.json`:
```json
{
  "idf.espIdfPath": "/home/<user>/.esp/esp-idf",
  "idf.pythonBinPath": "/home/<user>/.esp/espressif/python_env/idf6.1_py3.14_env/bin/python",
  "idf.toolsPath": "/home/<user>/.esp/espressif",
  "idf.port": "/dev/ttyACM0",
  "idf.adapterTargetName": "esp32c6",
  "C_Cpp.default.compileCommands": "${workspaceFolder}/build/compile_commands.json"
}
```
(заменить `<user>` на имя пользователя)

Также полезны `c_cpp_properties.json`, `tasks.json`, `launch.json` как в примере проекта.

## 5. Первый запуск
```bash
get_idf
cd <путь_к_проекту>
idf.py set-target esp32c6  # если новый проект
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Советы
- **WSL + USB**: лучше использовать usbipd, нежели монтирование COM-порта напрямую
- **JTAG**: для ESP32-C6 используется `esp_usb_jtag` (уже в прошивке)
- **Если не видит порт в WSL**: переподключить USB без зажатого BOOT, перепрокинуть через usbipd
- **Тулчейн хост-зависимостей**: `ninja`, `flex`, `bison`, `gperf` нужны для сборки
- **Python 3.14**: требует аккуратности с venv/ensurepip, иногда проще через `virtualenv`

## Текущие версии (референс)
- ESP-IDF: v6.1
- esptool: v5.6.0
- OpenOCD: v0.12.0-esp32-20260831
- Целевой чип: esp32c6
