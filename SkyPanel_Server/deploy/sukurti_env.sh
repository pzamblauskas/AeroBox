#!/usr/bin/env bash
# Sukuria .env su atsitiktiniais slaptažodžiais. Paleisti serveryje, AeroBox serverio aplanke:
#
#     bash deploy/sukurti_env.sh api.jusu-domenas.com jusu@pastas.com
#
# Jei .env jau yra, nieko nekeičia (slaptažodžiai lieka tie patys).
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -f .env ]; then echo ".env jau yra - nekeiciu."; exit 0; fi
DOMAIN="${1:?Nurodykite domena, pvz.: bash deploy/sukurti_env.sh api.jusu-domenas.com jusu@pastas.com}"
EMAIL="${2:?Nurodykite el. pasta (Let's Encrypt pranesimams)}"
umask 077
sed -e "s|^DOMAIN=.*|DOMAIN=${DOMAIN}|" \
    -e "s|^ACME_EMAIL=.*|ACME_EMAIL=${EMAIL}|" \
    -e "s|^DB_PASSWORD=.*|DB_PASSWORD=$(openssl rand -hex 24)|" \
    -e "s|^ADMIN_TOKEN=.*|ADMIN_TOKEN=$(openssl rand -hex 24)|" \
    -e "s|^INTERNAL_TOKEN=.*|INTERNAL_TOKEN=$(openssl rand -hex 24)|" \
    .env.example > .env
chmod 600 .env
mkdir -p backups geoip
echo "Sukurta .env (teises 600)."
echo "Valdymo puslapio raktas (ADMIN_TOKEN) - issisaugokite slaptazodziu tvarkykleje:"
grep '^ADMIN_TOKEN=' .env | cut -d= -f2
