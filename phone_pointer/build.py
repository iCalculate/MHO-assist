from pathlib import Path
import subprocess
import zipfile

root=Path(__file__).resolve().parent
out=root/'build';out.mkdir(exist_ok=True)
sdk=root.parent/'analysis/integrated_hud_20261112/android-sdk'
tools=sdk/'android-15';platform=sdk/'android-35/android.jar'
jdk=Path('C:/Program Files/JetBrains/PyCharm 2026.1/jbr/bin')
def run(args):subprocess.run([str(a) for a in args],check=True)
classes=out/'classes';classes.mkdir(exist_ok=True)
run([jdk/'javac.exe','-encoding','UTF-8','-source','8','-target','8','-bootclasspath',platform,'-d',classes,root/'PointerService.java'])
jar=out/'classes.jar'
with zipfile.ZipFile(jar,'w') as z:
    for p in classes.rglob('*.class'):z.write(p,p.relative_to(classes).as_posix())
run([jdk/'java.exe','-cp',tools/'lib/d8.jar','com.android.tools.r8.D8','--lib',platform,'--min-api','26','--output',out,jar])
apk=out/'pointer-unsigned.apk'
run([tools/'aapt.exe','package','-f','-M',root/'AndroidManifest.xml','-I',platform,'-F',apk])
with zipfile.ZipFile(apk,'a',zipfile.ZIP_DEFLATED) as z:z.write(out/'classes.dex','classes.dex')
aligned=out/'pointer-aligned.apk'
run([tools/'zipalign.exe','-f','4',apk,aligned])
key=out/'local-development.p12'
if not key.exists():run([jdk/'keytool.exe','-genkeypair','-keystore',key,'-storepass','local-build-only','-keypass','local-build-only','-alias','pointer','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=QtScrcpy Local Pointer'])
result=root/'qtscrcpy-pointer.apk'
run([jdk/'java.exe','-jar',tools/'lib/apksigner.jar','sign','--ks',key,'--ks-pass','pass:local-build-only','--out',result,aligned])
run([jdk/'java.exe','-jar',tools/'lib/apksigner.jar','verify',result])
print('Built',result)
