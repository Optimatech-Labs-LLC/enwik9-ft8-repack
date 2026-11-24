"""Reversibly pack ft8's binary prefix and model, or run its original decoder."""
from pathlib import Path
import argparse, hashlib, os, shutil, struct, subprocess, tempfile

from nrv import pack_prefix, unpack_prefix

FT8 = "2b3892ca5a43e1b25bcbbbb5bc79f95cc09331953b324333500e61a3b45eba5b"
ENWIK9 = "159b85351e5f76e60cbe32e04c677847a9ecba3adc79addab6f4c6c7aa3744bc"
MAGIC = b"FT8NRV03"
HEADER = struct.Struct("<8sIII")

def sha(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()

def transcode(work, operation, data):
    source, output, exe = work/"stream.in", work/"stream.out", work/"ft8-codec"
    if not exe.exists():
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                        str(Path(__file__).with_name("weight15.cpp")), "-o", str(exe)], check=True)
    source.write_bytes(data)
    subprocess.run([str(exe), operation, str(source), str(output)], check=True)
    return output.read_bytes()

def pack(source, target, work):
    if sha(source)!=FT8:
        raise ValueError("input does not match the published ft8 archive")
    with source.open("rb") as src, target.open("xb") as dst:
        src.seek(-16,os.SEEK_END)
        dict_len, _, text_len, weight_len = struct.unpack("<4I",src.read(16))
        exe_len = src.tell()-16-dict_len-text_len-weight_len
        src.seek(0)
        binary = pack_prefix(src.read(exe_len), lambda action, data: transcode(work, action, data))
        dictionary=src.read(dict_len)
        weights=transcode(work,"pack",src.read(weight_len))
        dst.write(HEADER.pack(MAGIC,len(binary),len(dictionary),len(weights)))
        dst.write(binary)
        dst.write(dictionary)
        dst.write(weights)
        shutil.copyfileobj(src,dst)

def expand(source, target, work):
    with source.open("rb") as src, target.open("xb") as dst:
        magic, exe_len, dict_len, weight_len = HEADER.unpack(src.read(HEADER.size))
        if magic!=MAGIC or exe_len+dict_len+weight_len+HEADER.size+16>source.stat().st_size:
            raise ValueError("invalid archive")
        dst.write(unpack_prefix(src.read(exe_len), lambda action, data: transcode(work, action, data)))
        dst.write(src.read(dict_len))
        dst.write(transcode(work,"unpack",src.read(weight_len)))
        shutil.copyfileobj(src,dst)
    if sha(target)!=FT8:
        raise ValueError("reconstructed ft8 SHA-256 mismatch")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("pack","expand","restore"))
    parser.add_argument("input",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    source=args.input.resolve()
    output=args.output.absolute()
    if output.exists():
        raise FileExistsError(output)
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        work=Path(tmp)
        result=work/"archive9"
        if args.action=="pack":
            pack(source,result,work)
        else:
            expand(source,result,work)
            if args.action=="restore":
                result.chmod(0o700)
                subprocess.run([str(result)], cwd=work, check=True)
                result=work/"enwik9_uncompressed"
                if result.stat().st_size!=1_000_000_000 or sha(result)!=ENWIK9:
                    raise ValueError("restored enwik9 failed verification")
        os.link(result,output)
if __name__=="__main__":
    main()

