# 📷 Sistem Camera Trap Berbasis IoT (ESP32-S3 & Telegram Bot)

Repositori ini berisi kode program dan dokumentasi proyek Tugas Akhir **Sistem Pemantauan Satwa (Camera Trap) Berbasis Internet of Things (IoT)** yang dirancang untuk pemantauan satwa liar di kawasan Gunung Muria.

Sistem ini mendeteksi pergerakan objek secara otomatis dan mengirimkan hasil tangkapan foto secara *real-time* melalui aplikasi pesan instan Telegram.

---

## 🎯 Fitur Utama

* **Deteksi Gerakan Otomatis:** Memanfaatkan sensor PIR (*Passive Infrared*) untuk memicu pengambilan gambar hanya saat terdeteksi pergerakan satwa.
* **Pengambilan Gambar Resolusi Tinggi:** Menggunakan modul kamera OV5640 yang terintegrasi langsung dengan mikrokontroler ESP32-S3.
* **Notifikasi Real-Time:** Mengirimkan hasil foto tangkapan langsung ke pengguna melalui integrasi **Telegram Bot API**.
* **Efisiensi Daya:** Dirancang untuk penggunaan pemantauan di area luar ruangan (*outdoor*) dan kawasan hutan.

---

## 🛠️ Spesifikasi Perangkat Keras (Hardware)

* **Mikrokontroler:** ESP32-S3
* **Modul Kamera:** OV5640
* **Sensor Gerak:** Sensor PIR (*Passive Infrared*)
* **Konektivitas:** Wi-Fi / Jaringan Internet untuk komunikasi API Telegram

---

## ⚙️ Alur Kerja Sistem

1. **Standby Mode:** Perangkat berada dalam mode siaga memantau area sekitar melalui sensor PIR.
2. **Trigger Detection:** Sensor PIR mendeteksi perubahan radiasi inframerah dari pergerakan satwa dan mengirimkan sinyal pemicu ke ESP32-S3.
3. **Image Capture:** ESP32-S3 mengaktifkan kamera OV5640 untuk mengambil foto objek.
4. **Data Transmission:** ESP32-S3 terhubung ke internet dan mengirimkan file gambar ke akun/grup penerima melalui **Telegram Bot**.

---

## 👨‍💻 Pengembang

**Faza Chansa Maulana, S.Kom**  
Lulusan S1 Teknik Informatika — Universitas Muria Kudus  
📧 Email: [fazachansa123@gmail.com](mailto:fazachansa123@gmail.com)
