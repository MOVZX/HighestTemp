4.  **Penulisan Output:** Suhu tertinggi yang terdeteksi kemudian ditulis ke file `/dev/shm/highesttemp`. Proses penulisan ini dilakukan secara atomik (menulis ke file sementara lalu mengganti nama) untuk menjamin konsistensi data.
5.  **Integrasi FanControl:** `fancontrol.service` Anda dapat dikonfigurasi untuk membaca nilai dari `/dev/shm/highesttemp` sebagai sumber suhu utamanya, memungkinkan kipas Anda merespons suhu puncak sistem secara real-time.

## Instalasi

Untuk membangun dan menginstal `HighestTemp`, ikuti langkah-langkah berikut:

1.  **Kloning Repositori:**
    ```bash
    git clone https://github.com/goghor/HighestTemp.git
    cd HighestTemp
    ```

2.  **Bangun Aplikasi:**
    Skrip `build.sh` akan secara otomatis mendeteksi apakah driver NVIDIA terinstal dan akan mengkompilasi aplikasi dengan dukungan NVIDIA NVML jika ditemukan.
    ```bash
    bash build.sh
    ```

3.  **Instalasi Layanan Sistem:**
    Setelah membangun aplikasi, Anda dapat menginstalnya sebagai layanan systemd:
    ```bash
    sudo bash build.sh install
    ```

## Penggunaan dengan FanControl

Setelah `HighestTemp` berjalan sebagai layanan, Anda dapat mengkonfigurasi `fancontrol.service` Anda untuk menggunakan `/dev/shm/highesttemp` sebagai sumber suhu. Edit file konfigurasi `fancontrol` Anda (biasanya `/etc/fancontrol`) dan atur variabel `FCTEMPS` atau sumber suhu lainnya untuk menunjuk ke file ini.

Contoh konfigurasi `fancontrol`:

```
# ... konfigurasi lainnya ...

FCTEMPS=/dev/shm/highesttemp

# ... atur kipas Anda untuk menggunakan FCTEMPS ...
```

Pastikan untuk me-restart layanan `fancontrol` setelah membuat perubahan:

```bash
sudo systemctl restart fancontrol.service
```

### Contoh Konfigurasi FanControl Lanjutan

Berikut adalah contoh konfigurasi `fancontrol` yang lebih rinci, mengintegrasikan output `HighestTemp` untuk mengontrol beberapa kipas berdasarkan suhu tertinggi sistem:

```ini
INTERVAL=1
DEVPATH=hwmon9=devices/platform/nct6687.2592
DEVNAME=hwmon9=nct6686
FCTEMPS=hwmon9/pwm1=/dev/shm/highesttemp hwmon9/pwm3=/dev/shm/highesttemp hwmon9/pwm4=/dev/shm/highesttemp hwmon9/pwm5=/dev/shm/highesttemp hwmon9/pwm6=/dev/shm/highesttemp
FCFANS=hwmon9/pwm1=hwmon9/fan1_input hwmon9/pwm3=hwmon9/fan3_input hwmon9/pwm4=hwmon9/fan4_input hwmon9/pwm5=hwmon9/fan5_input hwmon9/pwm6=hwmon9/fan6_input
MINTEMP=hwmon9/pwm1=55 hwmon9/pwm3=55 hwmon9/pwm4=55 hwmon9/pwm5=65 hwmon9/pwm6=65
MAXTEMP=hwmon9/pwm1=75 hwmon9/pwm3=75 hwmon9/pwm4=75 hwmon9/pwm5=75 hwmon9/pwm6=75
MINSTART=hwmon9/pwm1=150 hwmon9/pwm3=100 hwmon9/pwm4=100 hwmon9/pwm5=100 hwmon9/pwm6=125
MINSTOP=hwmon9/pwm1=100 hwmon9/pwm3=50 hwmon9/pwm4=50 hwmon9/pwm5=50 hwmon9/pwm6=50
MAXPWM=hwmon9/pwm1=255 hwmon9/pwm3=255 hwmon9/pwm4=255 hwmon9/pwm5=255 hwmon9/pwm6=255

# Contoh konfigurasi kipas di PC Saya
# pwm1 = Radiator Fans
# pwm2 = Radiator Pump
# pwm3 = Top + Rear Case Fans
# pwm4 = Bottom Case Fans 1
# pwm5 = Bottom Case Fans 2
```

**Penjelasan Konfigurasi:**

*   **`INTERVAL=1`**: Menentukan interval pembaruan (dalam detik) untuk `fancontrol`. Ini harus sesuai atau lebih besar dari interval pembaruan `HighestTemp` (`DT` di `highesttemp.c`).
*   **`DEVPATH` dan `DEVNAME`**: Mengidentifikasi perangkat `hwmon` yang digunakan. Anda mungkin perlu menyesuaikannya berdasarkan sistem Anda. Gunakan `pwmconfig` untuk membantu mengidentifikasi ini.
*   **`FCTEMPS`**: Ini adalah bagian terpenting untuk integrasi `HighestTemp`. Setiap entri `hwmonX/pwmY=/dev/shm/highesttemp` menginstruksikan `fancontrol` untuk menggunakan nilai dari `/dev/shm/highesttemp` sebagai sumber suhu untuk kipas yang terhubung ke `pwmY`. Dalam contoh ini, semua kipas (pwm1, pwm3, pwm4, pwm5, pwm6) dikontrol oleh suhu tertinggi yang disediakan oleh `HighestTemp`.
*   **`FCFANS`**: Memetakan output PWM ke input sensor kipas yang sesuai.
*   **`MINTEMP` dan `MAXTEMP`**: Menentukan rentang suhu (dalam derajat Celsius) di mana `fancontrol` akan menyesuaikan kecepatan kipas. `MINTEMP` adalah suhu di mana kipas mulai berputar lebih cepat dari `MINSTOP`, dan `MAXTEMP` adalah suhu di mana kipas mencapai kecepatan `MAXPWM`.
*   **`MINSTART`**: Kecepatan PWM minimum (0-255) di mana kipas akan mulai berputar.
*   **`MINSTOP`**: Kecepatan PWM minimum di mana kipas akan berhenti berputar.
*   **`MAXPWM`**: Kecepatan PWM maksimum (biasanya 255 untuk 100%).

Pastikan untuk menyesuaikan nilai-nilai ini agar sesuai dengan kipas dan preferensi pendinginan spesifik Anda. Selalu jalankan `sudo pwmconfig` untuk menghasilkan konfigurasi dasar yang sesuai dengan sistem Anda sebelum melakukan penyesuaian manual.
