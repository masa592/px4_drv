# PLEX PX-S1UD WinUSB ドライバ & BonDriver 利用ガイド

本リポジトリでは、PLEX PX-S1UD (V1.0 / V2.0) を Windows 上でカーネルドライバ署名の強制を無効化することなく、Windows 標準の **WinUSB** 経由で安定動作させるための **WinUSB ドライバ定義 (.inf)** と **BonDriver (`BonDriver_S1UD.dll` / `DriverHost_PX4.exe`)** を提供しています。

---

## 1. 対応機種・ハードウェアID

| 機種名 | USB VID / PID | Demod / Tuner | 備考 |
|---|---|---|---|
| **PLEX PX-S1UD V1.0** | `0x0511` / `0x0246` | Siano SMS1140 | 初代モデル |
| **PLEX PX-S1UD V2.0** | `0x3275` / `0x0080` | Siano SMS2270 (Rio) | 現行モデル (MyGica OEM互換) |
| **Siano Rio / Nova 互換機** | `0x187f` / `0x0600`, `0x0201` | Siano 系列 | 同系統チップ採用機 |

---

## 2. ドライバのインストール

Windows 標準の `WinUSB.sys` を PX-S1UD に割り当てます。以下のいずれかの方法で導入できます。

### 方法 A: Zadig を使用した簡単な導入 (推奨)

1. [Zadig 公式サイト](https://zadig.akeo.ie/) から Zadig をダウンロードして起動します。
2. メニューの **Options** → **List All Devices** にチェックを入れます。
3. ドロップダウンリストから **PX-S1UD** (または `VID: 0511, PID: 0246` / `VID: 3275, PID: 0080`) を選択します。
4. 対象ドライバの矢印の右側を **WinUSB (`WinUSB.sys`)** に指定します。
5. **Install Driver** (または **Replace Driver**) をクリックします。

### 方法 B: INF ファイルからのインストール

1. デバイスマネージャーを開き、PX-S1UD デバイスを右クリックして「ドライバーの更新」を選択します。
2. 「コンピューターを参照してドライバーを検索」を選択します。
3. リポジトリ内の `winusb/pkg/inf/` (または配布パッケージ内の `Driver` フォルダ) を指定してインストールを完了します。

---

## 3. ファームウェアの準備

Siano チップ（SMS1140 / SMS2270）は起動時にファームウェアのロードが必要です。
Linux 用ファームウェアファイル（`isdbt_rio.inp` または `isdbt_nova_12mhz_b0.inp`）を取得し、**`DriverHost_PX4.exe` / `BonDriver_S1UD.dll` と同じフォルダ** に配置してください。

- 配布元例: [Linux Firmware Repository (siano)](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/siano)
  - `isdbt_rio.inp` (PX-S1UD V2.0 / Rio 向け)
  - `isdbt_nova_12mhz_b0.inp` (PX-S1UD V1.0 / Nova 向け)

---

## 4. BonDriver の配置と設定 (DriverHost_PX4 統合方式)

TVTest や EDCB (EpgDataCap_Bon) などの BonDriver フォルダに以下のパッケージファイルを配置します。

```text
TVTest/ (または EDCB/BonDriver/)
  ├── DriverHost_PX4.exe
  ├── DriverHost_PX4.ini
  ├── WinSCard.dll
  ├── BonDriver_S1UD.dll
  ├── BonDriver_S1UD.ini
  ├── BonDriver_S1UD-T.ChSet.txt
  ├── PX-S1UD.inf
  └── isdbt_rio.inp (または isdbt_nova_12mhz_b0.inp)
```

---

## 5. 複数台の同時利用（複数枚刺し）

PC に 2 台以上の PX-S1UD を接続している場合でも、`DriverHost_PX4` のデバイス列挙および名前付きパイプアーキテクチャにより、各チューナーを自動検出して排他制御しながら安定して同時利用できます。

---

## 6. ビルド方法

### 前提環境
- Windows 10 / 11 (x64)
- Visual Studio 2022 (C++ によるデスクトップ開発ワークロード) または Visual Studio Build Tools

### 手動ビルド手順
1. コマンドプロンプトまたは PowerShell を開きます。
2. `winusb/px4_winusb.sln` を MSBuild でビルドします。
   ```cmd
   msbuild winusb\px4_winusb.sln /p:Configuration=Release-static /p:Platform=x64 /m
   ```

全機種パッケージングを含めてビルドする場合は、PowerShell で以下を実行してください:
```powershell
Set-Location winusb
.\build.ps1
```
`winusb/dist/BonDriver_S1UD_64bit/` および `32bit/` に必要なファイル一式が出力されます。
