from pathlib import Path


project_root = Path(SPECPATH).parent
profiles = [
    (str(path), "config")
    for path in sorted((project_root / "config").glob("*.conf"))
]

analysis = Analysis(
    [str(project_root / "ui" / "acoustic_ui.py")],
    pathex=[str(project_root)],
    binaries=[(str(project_root / "build" / "acoustic-transfer.exe"), ".")],
    datas=profiles,
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=1,
)

python_archive = PYZ(analysis.pure)

executable = EXE(
    python_archive,
    analysis.scripts,
    analysis.binaries,
    analysis.datas,
    [],
    name="AcousticFileTransfer",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
