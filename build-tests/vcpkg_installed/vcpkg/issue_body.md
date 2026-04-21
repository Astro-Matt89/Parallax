Package: vcpkg-make:x64-linux@2026-01-01

**Host Environment**

- Host: x64-linux
- Compiler: GNU 13.3.0
- CMake Version: 4.2.3
-    vcpkg-tool version: 2026-04-08-e0612b42ce44e55a0e630f2ee9d3c533a63d8bc1
    vcpkg-scripts version: b80e006657 2026-04-13 (8 days ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
Downloading automake-1.17.tar.gz, trying https://ftpmirror.gnu.org/gnu/automake/automake-1.17.tar.gz
error: curl operation failed with error code 6 (Couldn't resolve host name).
error: Not a transient network error, won't retry download from https://ftpmirror.gnu.org/gnu/automake/automake-1.17.tar.gz
Trying https://ftp.gnu.org/gnu/automake/automake-1.17.tar.gz
Trying https://www.mirrorservice.org/sites/ftp.gnu.org/gnu/automake/automake-1.17.tar.gz
error: curl operation failed with error code 6 (Couldn't resolve host name).
error: Not a transient network error, won't retry download from https://ftp.gnu.org/gnu/automake/automake-1.17.tar.gz
error: curl operation failed with error code 6 (Couldn't resolve host name).
error: Not a transient network error, won't retry download from https://www.mirrorservice.org/sites/ftp.gnu.org/gnu/automake/automake-1.17.tar.gz
note: If you are using a proxy, please ensure your proxy settings are correct.
Possible causes are:
1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to `https://address:port`.
This is not correct, because `https://` prefix claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr, etc...) is an HTTP proxy.
Try setting `http://address:port` to both HTTP_PROXY and HTTPS_PROXY instead.
2. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software. See: https://github.com/microsoft/vcpkg-tool/pull/77
The value set by your proxy might be wrong, or have same `https://` prefix issue.
3. Your proxy's remote server is out of service.
If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github.com/Microsoft/vcpkg/issues
CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:136 (message):
  Download failed, halting portfile.
Call Stack (most recent call first):
  ports/vcpkg-make/portfile.cmake:4 (vcpkg_download_distfile)
  scripts/ports.cmake:206 (include)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "parallax",
  "version-string": "0.1.0",
  "description": "Ground-based astronomical observatory simulator",
  "dependencies": [
    {
      "name": "sdl2",
      "features": [
        "vulkan"
      ]
    },
    "glm",
    "spdlog",
    "vulkan-memory-allocator",
    "doctest"
  ]
}

```
</details>
