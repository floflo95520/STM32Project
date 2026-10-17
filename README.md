# STM32Project Florian CHAPIN & Matias VINKOVIC

# Vidéo de demonstration sur le drive : 
https://drive.google.com/drive/folders/1M5sx4N0gDF9-othnfSCgVrka_PD5IR7o?usp=sharing

# 📡 Relais de message PC → STM32 → PC

Projet de transmission série : un message est envoyé depuis un **PC source**, traverse une chaîne de **N cartes STM32**, puis est affiché sur un **PC destinataire**.

```
┌─────────┐   USB   ┌─────────┐         ┌─────────┐   USB   ┌─────────┐
│  PC 1   │────────▶│ STM32 #1│──▶ … ──▶│ STM32 #N│────────▶│  PC 2   │
│ (Python)│  UART   │         │         │         │  UART   │(picocom)│
└─────────┘ 115200  └─────────┘         └─────────┘ 115200  └─────────┘
```

---

## 📑 Sommaire

- [Principe](#-principe)
- [Prérequis](#-prérequis)
- [Installation](#-installation)
- [Utilisation](#-utilisation)
  - [PC 1 : envoi du message](#pc-1--envoi-du-message)
  - [PC 2 : réception du message](#pc-2--réception-du-message)
- [Format des trames](#-format-des-trames)
- [Dépannage](#-dépannage)
- [Structure du dépôt](#-structure-du-dépôt)

---

## 🎯 Principe

1. Le **PC 1** envoie deux trames sur le port série de la première carte STM32 :
   1. une trame **INIT** qui initialise la chaîne ;
   2. une trame **MESSAGE** qui contient le texte à transmettre.
2. Les **N cartes STM32** relaient la trame de proche en proche.
3. Le **PC 2**, branché sur la dernière carte, lit le message avec **picocom**.

La liaison série est configurée en **115200 bauds** sur toute la chaîne.

---

## 🧰 Prérequis

| Côté | Outil | Rôle |
|------|-------|------|
| PC 1 | Python 3.8+ | Exécuter le script d'envoi |
| PC 1 | `pyserial` | Communication série en Python |
| PC 2 | `picocom` | Terminal série pour lire le message |
| Matériel | N cartes STM32 | Déjà flashées avec le firmware de relais |
| Matériel | Câbles USB | Un par PC, plus la liaison entre les cartes |

---

## ⚙️ Installation

### PC 1 (source)

```bash
pip install pyserial
```

### PC 2 (destinataire)

**macOS**
```bash
brew install picocom
```

**Debian / Ubuntu**
```bash
sudo apt install picocom
```

---

## 🚀 Utilisation

### PC 1 : envoi du message

1. Branche la **carte 1** en USB et repère son port série :

   ```bash
   # macOS
   ls /dev/tty.usbmodem*

   # Linux
   ls /dev/ttyACM*
   ```

2. Modifie la constante `PORT_CARTE_1` dans `send.py` avec le port trouvé.

3. Lance le script :

   ```bash
   python3 send.py
   ```

<details>
<summary><b>📄 Voir le script <code>send.py</code></b></summary>

```python
import serial
import time

PORT_CARTE_1 = '/dev/tty.usbmodem11203'   # À adapter selon ta machine

# --- Trame INIT : 0x7E + 23 octets nuls + CRC (0x9F 0x7B) ---
trame_init = bytes([0x7E] + [0x00] * 23 + [0x9F, 0x7B])

# --- Trame MESSAGE ---
texte = "HELLO CY-TECH ! "
payload = texte.encode('ascii')[:16]          # 16 octets max

header = bytes([0x7E, 0x03, len(payload), 0x00, 0x00, 0x00, 0x00, 0x00])
trame_msg = header + payload + bytes([0x00, 0x00])

with serial.Serial(PORT_CARTE_1, 115200, timeout=1) as ser:
    time.sleep(1)

    # Envoi INIT
    print("Envoi de l'INIT sur la Carte 1...")
    ser.write(trame_init)
    time.sleep(1)

    # Envoi MESSAGE
    print(f"Envoi du message : '{texte.strip()}'...")
    ser.write(trame_msg)
    time.sleep(0.5)

print("Terminé. Regarde la Carte 2 !")
```

</details>

> 💡 Pour changer le message, modifie simplement la variable `texte` (16 caractères ASCII maximum).

### PC 2 : réception du message

1. Branche la **dernière carte** STM32 en USB et repère son port :

   ```bash
   # macOS
   ls /dev/tty.usbmodem*

   # Linux
   ls /dev/ttyACM*
   ```

2. Ouvre le port avec **picocom** à 115200 bauds :

   ```bash
   # macOS
   picocom -b 115200 /dev/tty.usbmodemXXXX

   # Linux
   picocom -b 115200 /dev/ttyACM0
   ```

3. Lance ensuite `send.py` sur le PC 1 : le message s'affiche dans le terminal de picocom.

4. Pour quitter picocom : **`Ctrl + A`** puis **`Ctrl + X`**.

> ⚠️ Lance **picocom avant** d'exécuter le script sur le PC 1, sinon tu risques de rater le message.

---

```

