import pathlib,subprocess,sys,os
here=pathlib.Path(__file__).resolve().parent
work=pathlib.Path(sys.argv[1]);work.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,CARGO_TARGET_DIR=str(work/'target'))
subprocess.run(['cargo','build','--offline','--manifest-path',str(here/'rust/Cargo.toml')],env=env,check=True)
for script in ['model_diff.py','widget_diff.py']:
 subprocess.run([sys.executable,str(here/script),str(work)],check=True)
