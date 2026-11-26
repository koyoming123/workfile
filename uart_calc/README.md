# UART 計算機系統 + 網頁管理端 (模組化版本)

Raspberry Pi 4 上以 C 語言實作的 UART 文字計算機：
- **UART 端**：使用者用 PuTTY 登入後使用計算機 (`+ - * /` 與括號)。
- **網頁管理端**：管理者用瀏覽器開 `http://<Pi_IP>:5000/`，可以查看狀態、鎖定 / 解鎖角色、踢除 UART 使用者、查看日誌。
- **設定檔**：`config.txt`。**日誌檔**：`log.txt`。

這是原本單檔版 (`uart_calc.c`) 依職責拆成模組的版本，對外行為完全相同。

## 目錄結構

```
uart_calc/
├── Makefile
├── config.txt
├── src/
│   ├── main.c            程式進入點：初始化、建立執行緒、關機流程
│   ├── uart_session.c/h  UART 使用者介面：登入、行編輯、計算機模式
│   ├── web.c/h           網頁管理端：HTTP 解析、登入 session、頁面、JSON API
│   ├── calc.c/h          算式求值 (純函式，可單元測試)
│   ├── admin_cmd.c/h     管理命令：status / lock / unlock / kick / showlog / help / quit
│   ├── state.c/h         共享狀態 (內含互斥鎖)
│   ├── log.c/h           log.txt 寫入、修剪、讀取最後 N 行
│   ├── config.c/h        讀取與驗證 config.txt、UART 帳號表
│   ├── uart_hal.c/h      UART 硬體抽象層 (termios)
│   ├── role.c/h          角色定義
│   ├── util.c/h          trim()、parse_int()
│   └── strbuf.c/h        可自動變長的字串緩衝區
└── tests/
    ├── test_calc.c       calc 模組單元測試 (30 項)
    └── test_config.c     config 模組單元測試 (36 項)
```

## 模組分層

上層只呼叫下層，下層不知道上層的存在：

```
介面層   uart_session.c          web.c
            │      ╲          ╱    │
應用層      │       calc.c   admin_cmd.c
            │                ╱     │
服務層   state.c    log.c    config.c
            │
硬體層   uart_hal.c

工具     role.c  util.c  strbuf.c   (各層皆可使用)
```

| 設計決策 | 原因 |
|---|---|
| `calc` 不依賴任何模組 | 可以在 PC 上做單元測試，不需要接樹莓派 |
| 只有 `uart_hal` 使用 termios | 移植到 RTOS / MCU 時只要重寫這一個檔案 |
| 共享狀態全部藏在 `state.c` | 外部只能透過函式存取，鎖在函式內部處理，不可能忘記上鎖 |
| `admin_cmd` 不知道命令從哪裡來 | 網頁按鈕、命令列共用同一套邏輯；之後要加回 `nc` 的 TCP 介面也不用重寫命令 |
| 網頁端不直接操作 UART | `kick uart` 只設旗標，由 UART 執行緒自己結束 session，序列埠只有一個擁有者 |

## 編譯與測試

```bash
make
```
```bash
make test
```
```bash
make clean
```

`make test` 會編譯並執行兩組單元測試，只連結被測試的模組，不需要 UART 硬體。測試 config 時看到的 `[config] ...` 錯誤訊息是刻意測試錯誤設定所產生的，最後一行顯示 `passed` 即代表通過。

## 在 Raspberry Pi 4 上執行

### 1. 安裝編譯環境並下載專案

```bash
sudo apt update && sudo apt install -y build-essential git
```
```bash
git clone https://github.com/koyoming123/workfile && cd workfile/uart_calc && make
```

### 2. 啟用 UART (只需做一次)

```bash
sudo raspi-config
```

`Interface Options → Serial Port`：
- 「login shell over serial?」選 **No** (否則 getty 會搶走 `/dev/serial0`)
- 「serial port hardware enabled?」選 **Yes**

```bash
sudo usermod -aG dialout $USER
```
```bash
sudo reboot
```

### 3. 接線與 PuTTY

| 樹莓派 | USB-TTL (3.3V) |
|---|---|
| Pin 6 GND | GND |
| Pin 8 GPIO14 TX | RX |
| Pin 10 GPIO15 RX | TX |

PuTTY：Connection type = **Serial**，Speed = `115200`，Flow control = **None**。

### 4. 執行

```bash
cd ~/workfile/uart_calc && ./uart_calc
```

- UART：PuTTY 按 Enter 出現 `Login:`
- 網頁：`http://<Pi_IP>:5000/`，密碼 `admin123`
- 停止：`Ctrl+C` (會寫入 `shutdown` 日誌)

## config.txt

```
TCP_PORT=5000            # 網頁管理端 port
UART_DEVICE=/dev/serial0
UART_BAUD=115200
LOG_FILE=log.txt         # 必須是 log.txt
MAX_LOG_LINES=1000       # 超過時只保留最新的 N 行
ALLOW_GUEST=1            # 啟動時各角色計算機是否啟用
ALLOW_OPERATOR=1
ALLOW_ADMIN=1
ADMIN_PASSWORD=admin123  # 網頁管理端密碼
UART_TIMEOUT_SEC=1       # UART 讀取逾時 (1~25 秒)
USERS=alice:alice123:operator,bob:bob123:guest,root:root123:admin   # 選填
```

- 缺少任一必要欄位、數值不合法，或 `LOG_FILE` 不是 `log.txt`，程式會印出錯誤並以 exit code 1 結束。
- `UART_TIMEOUT_SEC` 透過 termios `VMIN=0, VTIME=秒數×10` 套用到每次讀取。逾時時 UART 執行緒會檢查網頁端的踢除請求，所以 `kick uart` 最多 `UART_TIMEOUT_SEC` 秒後生效。

## UART 端

```
Login: alice
Password:
Welcome, role=operator
Calculator Ready
> 2+3*4
14
> (10-2)/4
2
> help
Input arithmetic expression with + - * / and ()
Commands: help, status, quit
> quit
bye
```

- `guest` 永遠沒有計算機權限，輸入算式會得到 `Permission denied`。
- 角色被網頁端鎖定時，同樣回覆 `Permission denied`。

## 網頁管理端命令

| 命令 | 說明 |
|---|---|
| `status` | `UART user=alice role=operator state=active` + 各角色狀態 |
| `lock <role>` / `unlock <role>` | 鎖定 / 解鎖該角色使用計算機 |
| `kick uart` | 強制中斷 UART session |
| `showlog [n]` | 顯示最後 n 行 log (預設 10) |
| `help` | 說明 |
| `quit` | 登出網頁 |

![網頁管理端](web_admin.png)

## log.txt 格式

```
YYYY-MM-DD HH:MM:SS | SOURCE | USER | ACTION | RESULT | DETAIL
```

```
2026-05-25 10:00:01 | SYSTEM | - | startup | OK | service started ...
2026-05-25 10:00:10 | UART | alice | login | OK | role=operator
2026-05-25 10:00:20 | UART | alice | calc | OK | expr=2+3*4 result=14
2026-05-25 10:00:35 | WEB | admin | lock_role | OK | role=operator
2026-05-25 10:00:40 | UART | alice | calc | DENIED | expr=1+1 role locked
2026-05-25 10:01:00 | WEB | admin | kick_uart | OK | user=alice
2026-05-25 10:01:05 | UART | alice | logout | OK | kicked by admin
```

使用者輸入中的 `|` 與換行會被替換，確保每行格式固定。
