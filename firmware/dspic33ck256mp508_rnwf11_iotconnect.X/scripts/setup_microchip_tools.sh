#!/usr/bin/env bash

setup_microchip_tools() {
  local root_dir
  root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

  if command -v xc16-gcc >/dev/null 2>&1; then
    echo "Microchip XC16 toolchain is already available on PATH."
    return 0
  fi

  local candidates=(
    "/opt/microchip/xc16/v2.10/bin"
    "/usr/local/microchip/xc16/v2.10/bin"
    "$HOME/.local/microchip/xc16/v2.10/bin"
    "$HOME/microchip/xc16/v2.10/bin"
    "/opt/microchip/xc16/current/bin"
    "/Applications/microchip/xc16/v2.10/bin"
  )

  local dir
  for dir in "${candidates[@]}"; do
    if [ -x "$dir/xc16-gcc" ]; then
      export PATH="$dir:$PATH"
      echo "Microchip XC16 toolchain found at $dir"
      return 0
    fi
  done

  echo "Microchip XC16 toolchain not found in the expected install locations."
  echo "Install MPLAB XC16 from the official Microchip page:"
  echo "  https://www.microchip.com/en-us/tools-resources/develop/mplab-xc-compilers"
  echo "Recommended install locations:"
  echo "  /opt/microchip/xc16/v2.10/bin"
  echo "  $HOME/.local/microchip/xc16/v2.10/bin"
  echo "After installation, re-run:"
  echo "  source $root_dir/scripts/setup_microchip_tools.sh"
  echo "  make -C $root_dir/bldc.X build"
  return 1
}

setup_microchip_tools "$@"
