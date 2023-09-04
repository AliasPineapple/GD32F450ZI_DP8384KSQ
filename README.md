## README ##

Use `LWIP` & `MbedTLS` on `GD32F450` with PHY `DP83848K`

## lwip-2.1.2 Modify ##

1. `lwip-2.1.2\src\api\if_api.c `

Line 51 add `int errno;` to fix bug

2. `lwip-2.1.2\src\apps\http\http_client.c` & `lwip-2.1.2\src\include\lwip\apps\http_client.h` & `lwip-2.1.2\src\include\lwip\apps\httpd_opts.h`

Add support to `HTTP POST`

3. `lwip-2.1.2\src\apps\sntp\sntp.c`

Line 138 add `extern int set_rtc_time_by_sntp(uint32_t value);` to set rtc time

4. Rrefer to [LWIP应用开发实战指南](https://doc.embedfire.com/net/lwip/zh/latest/doc/chapter9/chapter9.html)
