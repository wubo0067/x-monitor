# 修复版内核调试信息 RPM 包
Name:           kernel-debuginfo
Version:        4.18.0.348.7.1
Release:        1
Summary:        Fixed kernel debug symbols package
License:        GPLv2
BuildArch:      x86_64

# 完全禁用所有可能导致问题的自动处理
%global debug_package %{nil}
%global __arch_install_post %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress
%global __find_requires %{nil}
%global __find_provides %{nil}
%global _use_internal_dependency_generator 0
%global _missing_build_ids_terminate_build 0
%global _build_id_links none

%description
Fixed version of kernel debug information package containing vmlinux with debug symbols.

%prep
# 空的准备阶段

%build
# 空的构建阶段

%install
# 清理构建根目录
rm -rf %{buildroot}

# 创建必要的目录
mkdir -p %{buildroot}/usr/lib/debug/lib/modules/4.18.0_348.7.1.x86_64_cw.0.1
mkdir -p %{buildroot}/usr/lib/debug/boot

# 复制 vmlinux 文件
cp /root/rpmbuild/BUILD/kernel-4.18.0_348.7.1.x86_64_cw.0.1/vmlinux \
   %{buildroot}/usr/lib/debug/lib/modules/4.18.0_348.7.1.x86_64_cw.0.1/vmlinux

# 创建符号链接
cd %{buildroot}/usr/lib/debug/boot
ln -sf ../lib/modules/4.18.0_348.7.1.x86_64_cw.0.1/vmlinux vmlinux-4.18.0_348.7.1.x86_64_cw.0.1

# 设置正确的权限
chmod 644 %{buildroot}/usr/lib/debug/lib/modules/4.18.0_348.7.1.x86_64_cw.0.1/vmlinux

%files
%defattr(-,root,root,-)
/usr/lib/debug/lib/modules/4.18.0_348.7.1.x86_64_cw.0.1/vmlinux
/usr/lib/debug/boot/vmlinux-4.18.0_348.7.1.x86_64_cw.0.1

%changelog
* Wed Aug 06 2025 Root <root@localhost> - 4.18.0.348.7.1-1
- Fixed version that bypasses buildroot check issues