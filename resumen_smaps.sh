#!/bin/bash
# resumen_smaps.sh - Resume /proc/PID/smaps por region (Rss, Private_Dirty, Shared_Clean)
# Juan Diego Rojas Rey - 1152512
# Uso: ./resumen_smaps.sh PID
pid=${1:?uso: $0 PID}
awk '
  /^[0-9a-f]+-[0-9a-f]+ / {                 # cabecera de una region
      if (nombre != "") printf "%-20s %-5s %6d %8d %8d\n", rango, perm, rss, pd, sc
      split($1, r, "-"); rango = substr($1, 1, 20)
      perm = $2; nombre = ($6 == "" ? "[anonimo]" : $6)
      rss = pd = sc = 0
      next
  }
  /^Rss:/            { rss = $2 }
  /^Private_Dirty:/  { pd  = $2 }
  /^Shared_Clean:/   { sc  = $2 }
  END { printf "%-20s %-5s %6d %8d %8d\n", rango, perm, rss, pd, sc }
' /proc/$pid/smaps | sort -k3 -n -r | head -${2:-12}
