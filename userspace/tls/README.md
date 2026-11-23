# TLS no Loop-OS

- `bearssl/` - copia da biblioteca BearSSL (https://bearssl.org, licenca MIT em `bearssl/LICENSE.txt`),
  sem os arquivos de teste e sem `sysrng.c` (nao ha fonte de entropia do SO).
- `anchors.inc` - 118 certificados raiz da Mozilla (bundle do curl.se) convertidos com `brssl ta`.
  Para atualizar: baixe `cacert.pem`, rode `brssl ta cacert.pem > anchors.inc`.
- `tls.c` + `include/tls.h` - cliente TLS bloqueante sobre o socket TCP do Loop-OS.
- `shim/string.h` - cabecalho minimo para compilar a BearSSL sem libc.

Entropia: RDRAND (quando a CPU tem) misturado com rdtsc, tempo e relogio, resumido com SHA-256.
O relogio vem do RTC (`unixtime()`); se estiver errado a verificacao do certificado falha.
So o `wget` e o `navegador` linkam a biblioteca (`TLS_PROGS` no Makefile).
