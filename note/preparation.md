都说万事开头难，但是搭建6.s081(6.828)的环境也太难了。因为是基于MAC M1版本搭建的环境，下面我将阐述我搭建的时候遇到的坑和解决步骤。

[tools](https://pdos.csail.mit.edu/6.828/2021/tools.html):这个是官方的指导文档，我会分步骤解说遇到的问题和难点。



环境: Mac M1，能科学上网。



1. First, install developer tools

```sh
xcode-select --install
```

没有什么好说的，苹果电脑必须安装的。

2. Next, install [Homebrew](https://brew.sh/), a package manager for macOS:

```sh
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

最新版的`Homebrew`会安装到/opt/homebrew目录下。可能你需要配置一下环境变量

3. Next, install the [RISC-V compiler toolchain](https://github.com/riscv/homebrew-riscv):

   ```sh
   $ brew tap riscv/riscv
   $ brew install riscv-tools
   ```

这里是MAC M1最难的地方，主要原因是因为`brew install riscv-tools`目前还不支持arm架构。

解决方法:我们需要通过Rosetta2将arm转译成x86.虽然会损失部分性能，但是对于课程的学习是绰绰有余。



在命令行执行

 ```sh
 /usr/sbin/softwareupdate --install-rosetta --agree-to-license`
 arch -x86_64 zsh
 cd /usr/local && mkdir homebrew
 curl -L https://github.com/Homebrew/brew/tarball/master | tar xz --strip 1 -C homebrew
 ```

建议分步执行，不要一起复制。这相当于在同一台电脑上安装了两个homebrew。但是根据homebrew的要求，想要安装x86转译的软件，必须放在/usr/local之下。

接着我们时指定编译的架构，并且单独指定homebrew位置。

```sh
arch -x86_64 /usr/local/homebrew/bin/brew install riscv/riscv
arch -x86_64 /usr/local/homebrew/bin/brew install riscv-tools
```

会下载非常多的东西，并且需要好一会等待完成。


4. Finally, install QEMU:

```sh
brew install qemu
```



温馨提醒:如果哪一步执行成功，但是无法继续。请添加环境变量。



参考文档:

https://stackoverflow.com/questions/64963370/error-cannot-install-in-homebrew-on-arm-processor-in-intel-default-prefix-usr#

https://zhayujie.com/mit6828-env.html#comment-187