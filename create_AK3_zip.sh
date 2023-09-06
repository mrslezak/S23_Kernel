rm AnyKernel3/*.img
cp out/msm---gki/dist/boot.img AnyKernel3
cp out/msm---gki/dist/dtbo.img AnyKernel3
cp out/msm---gki/dist/vendor_dlkm.img AnyKernel3
cp out/msm---gki/dist/vendor_boot.img AnyKernel3
cd AnyKernel3
zip -r8 kernel_installer.zip .
mv kernel_installer.zip ../
