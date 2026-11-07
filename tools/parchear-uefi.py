import struct,zlib,lzma,hashlib,sys,uuid
src,out=sys.argv[1],sys.argv[2]; usb='--usb' in sys.argv
img0=open(src,'rb').read()
ks,ka,rs,ra,ss,sa,ta,ps,hv,osv=struct.unpack_from('<10I',img0,8)
al=lambda x:(x+ps-1)//ps*ps
d=zlib.decompressobj(31); fd=bytearray(d.decompress(img0[ps:ps+ks])); dtb=d.unused_data
r=img0[ps+al(ks):ps+al(ks)+rs]
LZ=uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf').bytes_le
g=fd.find(LZ)
while fd[g-1]!=2: g=fd.find(LZ,g+1)
sec=g-4; o=sec-24
assert fd[sec+3]==2 and fd[o+18]==0x0b, (hex(sec),fd[o+18])
ssz=int.from_bytes(fd[sec:sec+3],'little'); doff=struct.unpack_from('<H',fd,sec+20)[0]
oldend=o+int.from_bytes(fd[o+20:o+23],'little')
dec=bytearray(lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(bytes(fd[sec+doff:sec+ssz])))
print('ffs',hex(o),'sec',hex(sec),'dec',len(dec))
SMEM=struct.pack('<6I',1,5,2048,0,0,0)
def ent(n,a,l,at): n=n.encode(); return n+b'\0'*(32-len(n))+struct.pack('<QQ',a,l)+at
def patch(blob,lab):
    c=0
    for on,oa,nn,na,nl,at in [('DBI Dump',0x56700000,'PIL ADSP IPA GPU',0x53F00000,0x01717000,SMEM),
        ('WtShareMem',0x68000000,'QSEE TZApps',0x60000000,0x03900000,SMEM),
        ('VIDEO_CC',0x05B00000,'XBL AOP SEC Apps',0x45E00000,0x00200000,SMEM),
        ('GPU_GMU_CX_BLK',0x0597D000,'SS Plog Ramoops',0x71100000,0x00300000,SMEM),
        ('DXE Heap',0x53F00000,'DXE Heap',0x63900000,0x02800000,None)]:
        n=on.encode()+b'\0'; i=0
        while True:
            i=blob.find(n,i)
            if i<0: break
            if struct.unpack_from('<Q',blob,i+32)[0]==oa:
                at2=bytes(blob[i+48:i+72]) if at is None else at; blob[i:i+72]=ent(nn,na,nl,at2); c+=1
            i+=1
    print(lab,'mem entries',c)
patch(fd,'fd'); patch(dec,'dxe')
if usb:
    i=dec.find(b'SSDT'); 
    while dec[i+10:i+16]!=b'SAMSUN' and i>=0: i=dec.find(b'SSDT',i+1)
    ln=struct.unpack_from('<I',dec,i+4)[0]; p=dec[i:i+ln].find(b'URS0\x14\x09_STA\x00\xa4\x0a')
    if p>=0:
        p+=13; old=dec[i+p]; dec[i+p]=0x0F; dec[i+9]=(dec[i+9]-(0x0F-old))&0xFF
        print('URS0 _STA',old,'->15', sum(dec[i:i+ln])%256)
    else: print('URS0 _STA pattern not found (maybe already enabled)')
filt=[{'id':lzma.FILTER_LZMA1,'dict_size':1<<24,'lc':3,'lp':0,'pb':2,'preset':9|lzma.PRESET_EXTREME}]
comp=bytes([0x5d])+struct.pack('<I',1<<24)+struct.pack('<Q',len(dec))+lzma.compress(bytes(dec),format=lzma.FORMAT_RAW,filters=filt)
def hs(h): h=bytearray(h); h[17]=0; h[23]=0; return sum(h)&0xFF
nsec=bytearray(fd[sec:sec+doff]); nsz=doff+len(comp); nsec[0:3]=nsz.to_bytes(3,'little')
hdr=bytearray(fd[o:o+24]); hdr[20:23]=(24+nsz).to_bytes(3,'little'); hdr[16]=0; hdr[16]=(-hs(hdr))&0xFF
body=bytes(hdr)+bytes(nsec)+comp; assert len(body)<=oldend-o
fd[o:oldend]=body+b'\xff'*(oldend-o-len(body))
c=zlib.compressobj(9,zlib.DEFLATED,31); nk=c.compress(bytes(fd))+c.flush()+dtb
def sha(*bs):
    h=hashlib.sha1()
    for b in bs: h.update(b); h.update(struct.pack('<I',len(b)))
    return h.digest()
hd=bytearray(img0[:ps]); struct.pack_into('<I',hd,8,len(nk)); hd[576:608]=sha(nk,r,b'',b'')+b'\0'*12
open(out,'wb').write(bytes(hd)+nk+b'\0'*(al(len(nk))-len(nk))+r+b'\0'*(al(rs)-rs))
# verify
im=open(out,'rb').read(); f2=zlib.decompressobj(31).decompress(im[ps:ps+len(nk)]); assert f2==bytes(fd); print('ok',out)
print('Xhci in dxe:', dec.count('XhciDxe'.encode('utf-16le')))
