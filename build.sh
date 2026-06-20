#!/bin/bash
# build.sh - Compila session-switcher en Fedora o Arch/CachyOS
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "==> Detectando distro e instalando dependencias..."

if command -v dnf &>/dev/null; then
    sudo dnf install -y gcc systemd-devel pkgconf-pkg-config
elif command -v pacman &>/dev/null; then
    sudo pacman -S --needed --noconfirm gcc systemd-libs pkgconf
else
    echo "Distro no reconocida automaticamente."
    echo "Instala manualmente: gcc, systemd-devel (Fedora) o systemd-libs (Arch), y pkg-config."
    exit 1
fi

echo "==> Compilando..."
gcc -O2 -Wall -Wextra \
    $(pkg-config --cflags libsystemd) \
    -o session-switcher main.c \
    $(pkg-config --libs libsystemd)

echo "==> Compilado correctamente: ./session-switcher"
echo
echo "Prueba antes de instalar (ida y vuelta varias veces):"
echo "  ./session-switcher gamescope"
echo "  ./session-switcher plasma"
echo
echo "Para instalarlo en lugar del script bash actual:"
echo "  sudo cp /usr/bin/steamos-session-select /usr/bin/steamos-session-select.bash.bak"
echo "  sudo install -Dm755 session-switcher /usr/bin/steamos-session-select"
