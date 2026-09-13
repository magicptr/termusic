# termusic

termusic 是一款轻量、键盘驱动的 MPD（Music Player Daemon）终端音乐客户端。它提供音乐库浏览、播放控制、歌曲搜索、播放列表管理、主题切换、播放历史、插件扩展和频谱可视化，并支持连接本机或远程 MPD 服务。

## 依赖

- 支持 C++20 的编译器
- CMake 3.20 或更高版本
- Git
- Meson
- Ninja
- 可访问的 MPD 服务

Ubuntu / Debian：

```bash
sudo apt update
sudo apt install build-essential cmake git meson ninja-build
```

Fedora：

```bash
sudo dnf install gcc-c++ cmake git meson ninja-build
```

FTXUI、libmpdclient 和 kissfft 会在首次构建时自动下载并静态链接，无需单独安装对应的开发包。
如果没有可连接的远程 MPD 服务，还需在本机安装并配置 `mpd`。

## 克隆与使用

```bash
git clone https://github.com/magicptr/termusic.git
cd termusic
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j
./build/termusic
```

termusic 默认连接 `127.0.0.1:6600`。连接远程 MPD 服务时可使用：

```bash
./build/termusic --host <MPD_HOST> --port <MPD_PORT>
```

## 安装

安装到系统：

```bash
sudo cmake --install build
termusic
```

仅安装到当前用户：

```bash
cmake --install build --prefix "$HOME/.local"
termusic
```

如果 `$HOME/.local/bin` 不在 `PATH` 中，可直接运行：

```bash
"$HOME/.local/bin/termusic"
```
