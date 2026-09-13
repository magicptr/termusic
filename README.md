# termusic

termusic 是一款轻量、键盘驱动的 MPD（Music Player Daemon）终端音乐客户端。它提供音乐库浏览、播放控制、歌曲搜索、播放列表管理、主题切换、播放历史、插件扩展和频谱可视化，并支持连接本机或远程 MPD 服务。

## 演示

![项目演示](./docs/images/list.png)
![项目演示](./docs/images/play.png)

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

## 克隆

```bash
git clone https://github.com/magicptr/termusic.git
```

## 构建

```bash
cd termusic
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j
```

## 运行

```bash
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

## 快捷键操作

### 全局

| 快捷键 | 操作 |
| --- | --- |
| `q` | 退出程序 |
| `Esc` | 取消或返回 |
| `Space` | 播放或暂停 |
| `r` | 切换循环播放 |
| `s` | 切换随机播放 |
| `1` | 切换到音乐库（Vault） |
| `2` | 切换到设置（Core） |
| `i` | 进入或退出播放界面 |

### 导航

| 快捷键 | 操作 |
| --- | --- |
| `j` / `k` | 向下或向上移动 |
| `h` / `l` | 返回左侧面板或进入右侧面板 |
| `Enter` | 打开、确认或播放所选歌曲 |
| `g g` / `G` | 跳到第一项或最后一项 |
| `PageDown` / `PageUp` | 向下或向上翻页 |
| `Ctrl+d` / `Ctrl+u` | 在歌曲列表中向下或向上翻页 |

### 音乐库与播放列表

| 快捷键 | 操作 |
| --- | --- |
| `/` | 搜索当前列表 |
| `n` / `N` | 跳到下一个或上一个搜索结果 |
| `a` | 创建播放列表 |
| `r` | 重命名所选播放列表 |
| `d d` | 删除所选歌曲或播放列表 |
| `y y` | 复制所选歌曲到寄存器 |
| `p` | 将寄存器中的歌曲粘贴到播放列表 |
| `v` | 进入可视选择模式 |
| `K` / `J` | 将播放队列中的歌曲上移或下移 |

在可视选择模式中使用 `j`、`k` 扩展选择范围，按 `y` 复制、`d` 删除，按 `Esc` 退出。

### 播放界面

| 快捷键 | 操作 |
| --- | --- |
| `h` / `l` | 上一首或下一首 |
| `j` / `k` | 降低或提高音量 |
| `,` / `.` | 后退或快进 |
| `Space` | 播放或暂停 |
| `i` / `Esc` | 退出播放界面 |

除固定的退出键 `q` 外，快捷键可以在 **Core → Keybindings** 中修改；按 `R` 可在该页面恢复全部默认键位。

---

项目目前还在完善，如果在使用中有问题或有好的建议请[联系我](mailto:yiwithming@gmail.com)
