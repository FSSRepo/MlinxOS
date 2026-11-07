# Steward DNS (Windows)

Servidor autoritativo mínimo para desarrollo local, sin dependencias externas. Responde registros DNS A para una o más zonas usando UDP/IPv4 y no cambia la configuración DNS de Windows.

## Compilar y probar en este PC

```powershell
cmake -S . -B build
cmake --build build --config Release
.
build\Release\steward-dns.exe vm.com,test.com 192.168.123.56 53 127.0.0.1
```

En otra consola, prueba la consulta directamente contra este servicio:

```powershell
nslookup vm.com 127.0.0.1
```

El resultado esperado es `192.168.123.56`. El servicio escucha solo en loopback. No convierte `vm.com` en el DNS global de Windows para otras aplicaciones; para eso habría que cambiar el DNS de la interfaz de red.

Uso: `steward-dns [dominio[,dominio...]] [IPv4-respuesta] [puerto] [IP-bind]`. El puerto predeterminado es `5353`, reservado habitualmente para mDNS. En este PC Brave usa UDP `5353`, por lo que el ejemplo usa el puerto DNS normal `53` en `127.0.0.1`. Para que otros equipos consulten el servidor, enlaza a `0.0.0.0` y permite UDP 53 en el firewall de la red privada.

Solo implementa consultas UDP A de una zona exacta. Los demás nombres reciben NXDOMAIN; no hay recursión, TCP fallback, IPv6, múltiples registros ni edición de configuración del sistema.
