Set-Location 'E:\KuroganeOS'
Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Process -FilePath 'E:\KuroganeOS\tools\qemu\qemu-system-x86_64.exe' -ArgumentList @('-machine','q35','-m','512M','-drive','if=pflash,format=raw,readonly=on,file=E:\KuroganeOS\tools\qemu\share\edk2-x86_64-code.fd','-drive','if=pflash,format=raw,unit=1,snapshot=on,file=E:\KuroganeOS\tools\qemu\share\edk2-i386-vars.fd','-cdrom','E:\KuroganeOS\kurogane.iso','-net','none','-display','gtk') -WindowStyle Normal
