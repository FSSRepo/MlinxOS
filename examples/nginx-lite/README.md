# nginx-lite

Proxy inverso HTTP/1.1 en C++17. Un solo ejecutable atiende HTTP y HTTPS simultáneamente. HTTPS usa mbedTLS desde `examples/external/mbedtls`, sin OpenSSL.

## Compilación

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

HTTPS se incluye por defecto; `-DNGINX_LITE_TLS=OFF` genera una versión solo HTTP. La compilación con TLS también genera `nginx-lite-cert` para crear certificados de prueba.

## Configuración

Pasa uno o más archivos o directorios como argumentos. Para un directorio se leen sus archivos `.conf` en orden alfabético, sin recorrer subdirectorios:

```sh
./build/nginx-lite -c conf.d
./build/nginx-lite -c conf.d/linweb.conf -c conf.d/node-lite.conf
./build/nginx-lite conf.d/linweb.conf conf.d/node-lite.conf
```

Las configuraciones de ejemplo están en `conf.d/`. Por ejemplo:

```nginx
server {
    listen 80;
    server_name node-lite.com;
    location / {
        proxy_pass http://127.0.0.1:8400;
    }
}
```

Para HTTPS usa `listen 443 ssl;` y añade `ssl_certificate` y `ssl_certificate_key`. Las rutas relativas de certificados se resuelven respecto a la carpeta del archivo `.conf`. Para los ejemplos incluidos:

```sh
mkdir -p runtime
./build/nginx-lite-cert linweb.com runtime/linweb.com.crt runtime/linweb.com.key
./build/nginx-lite -c conf.d
```

Se admiten bloques `server` en el nivel principal o dentro de `http { ... }`, comentarios `#`, varios nombres exactos en `server_name`, y varios servidores compartiendo un puerto. Se admiten `listen <puerto> [ssl]`, `server_name`, `ssl_certificate`, `ssl_certificate_key` y una única `location /` con `proxy_pass http://host:puerto;` por servidor. Las directivas desconocidas producen un error; no se admite toda la configuración de Nginx. Todos los sitios HTTPS deben compartir el mismo certificado y clave, que deben cubrir sus nombres.

También se admiten los argumentos `--http <dominio> <puerto> <backend> <puerto-backend>` y `--https <dominio> <puerto> <backend> <puerto-backend> <certificado> <clave>`, y la forma anterior para un solo sitio.

Los puertos 80 y 443 pueden requerir privilegios de administrador. El DNS debe resolver los nombres a la IP de este servidor. El certificado generado es autofirmado y es solo para pruebas locales; los navegadores mostrarán un aviso si no confían en él.

## Límites

IPv4, conexiones atendidas secuencialmente, cabeceras hasta 64 KiB y cuerpos con `Content-Length` hasta 16 MiB. Sin WebSocket, cuerpos chunked, balanceo, reescrituras, SNI para distintos certificados ni configuración completa de Nginx.
