#!/usr/bin/env bash
# Naujo Ubuntu 22.04 / 24.04 serverio paruošimas AeroBox serveriui. Paleisti VIENĄ kartą kaip root:
#
#     sudo bash deploy/paruosti_serveri.sh
#
# Ką daro: atnaujina sistemą, įjungia automatinius saugumo atnaujinimus ir ugniasienę (tik SSH, 80, 443),
# įdiegia fail2ban (blokuoja slaptažodžių spėliojimą per SSH) ir Docker, sukuria 2 GB swap (jei jo nėra).
# Prisijungimą SSH slaptažodžiu išjungia TIK tada, kai serveryje jau yra SSH raktas, kad neužsirakintumėte.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "Paleiskite su sudo"; exit 1; }
export DEBIAN_FRONTEND=noninteractive

echo "== 1/6 Sistemos atnaujinimas"
apt-get update -y
apt-get upgrade -y
apt-get install -y ca-certificates curl gnupg ufw fail2ban unattended-upgrades openssl

echo "== 2/6 Automatiniai saugumo atnaujinimai"
cat > /etc/apt/apt.conf.d/20auto-upgrades <<'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF

echo "== 3/6 Ugniasiene: tik SSH, HTTP, HTTPS"
ufw default deny incoming
ufw default allow outgoing
ufw allow OpenSSH
ufw allow 80/tcp
ufw allow 443/tcp
ufw allow 443/udp
ufw --force enable

echo "== 4/6 fail2ban (SSH)"
cat > /etc/fail2ban/jail.d/sshd.local <<'EOF'
[sshd]
enabled = true
maxretry = 5
findtime = 10m
bantime = 1h
EOF
systemctl enable --now fail2ban
systemctl restart fail2ban

echo "== 5/6 SSH"
if [ -s /root/.ssh/authorized_keys ] || ls /home/*/.ssh/authorized_keys >/dev/null 2>&1; then
  cat > /etc/ssh/sshd_config.d/90-aerobox.conf <<'EOF'
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin prohibit-password
MaxAuthTries 4
EOF
  sshd -t && systemctl reload ssh 2>/dev/null || systemctl reload sshd
  echo "   prisijungimas slaptazodziu isjungtas (lieka tik SSH raktai)"
else
  echo "   !! SSH rakto nerasta - prisijungimas slaptazodziu PALIKTAS. Idekite rakta ir paleiskite skripta dar karta."
fi

echo "== 6/6 Docker"
if ! command -v docker >/dev/null; then
  install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  chmod a+r /etc/apt/keyrings/docker.asc
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
    > /etc/apt/sources.list.d/docker.list
  apt-get update -y
  apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
fi
systemctl enable --now docker

if ! swapon --show | grep -q .; then
  fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile && swapon /swapfile
  echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

echo
echo "PARUOSTA. Toliau (README.md): bash deploy/sukurti_env.sh DOMENAS EL.PASTAS  ir  docker compose up -d --build"
