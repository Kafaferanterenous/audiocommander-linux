#!/usr/bin/env bash
set -euo pipefail

echo "=== AudioCommander Samba Setup ==="
echo ""

# 1. Set Samba password for user 'dragon'
echo "[1/3] Setting Samba password for user 'dragon'..."
echo "(You'll be asked to enter the Samba password twice)"
sudo smbpasswd -a dragon

# 2. Append share definitions
echo "[2/3] Adding share definitions to /etc/samba/smb.conf..."

if ! grep -q '^\[GPT_Models\]' /etc/samba/smb.conf; then
sudo tee -a /etc/samba/smb.conf > /dev/null << 'EOF'

[GPT_Models]
   comment = GPT Models (NTFS Black 500GB)
   path = /media/dragon/D1_NTFS_Black500Gb/GPT_Models
   browseable = yes
   read only = no
   valid users = dragon
   create mask = 0775
   directory mask = 0775

[D3_NTFS_500GB]
   comment = D3 NTFS 500GB
   path = /media/dragon/D3_NTFS_500GB
   browseable = yes
   read only = no
   valid users = dragon
   create mask = 0775
   directory mask = 0775
EOF
echo "Shares added."
else
echo "Shares already configured, skipping."
fi

# 3. Restart Samba
echo "[3/3] Restarting Samba..."
sudo systemctl restart smbd

echo ""
echo "=== Done! ==="
echo ""
echo "Access from Windows/Linux/Mac:"
echo "  \\<this-machine-ip>\\GPT_Models"
echo "  \\<this-machine-ip>\\D3_NTFS_500GB"
echo ""
echo "Your LAN IP:"
hostname -I
