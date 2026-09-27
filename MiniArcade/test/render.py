from PIL import Image, ImageDraw, ImageFont
import glob, os
S=4
def render(path):
    rows=[]; texts=[]
    for line in open(path):
        if line.startswith('TEXT '):
            _,x,y,w,h,c,s = line.rstrip('\n').split(' ',6)
            texts.append((int(x),int(y),int(w),int(h),int(c),s))
        elif line.strip('\n'):
            rows.append(line.rstrip('\n'))
    img=Image.new('RGB',(128*S,64*S),(8,8,12))
    d=ImageDraw.Draw(img)
    # two colour panel: top 16 rows are yellow, the rest blue
    d.rectangle([0,0,128*S,16*S-1],fill=(22,18,4))
    d.rectangle([0,16*S,128*S,64*S],fill=(4,10,22))
    for y,r in enumerate(rows):
        for x,ch in enumerate(r):
            if ch=='#': d.rectangle([x*S,y*S,x*S+S-1,y*S+S-1],fill=(255,214,60) if y<16 else (110,205,255))
    for (x,y,w,h,c,s) in texts:
        col=((255,214,60) if y<16 else (110,205,255)) if c else (8,8,12)
        if c==0: d.rectangle([x*S,y*S,(x+w)*S,(y+h)*S],fill=(255,214,60) if y<16 else (110,205,255))
        f=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', int(h*S*0.95))
        d.text((x*S, y*S-2), s, fill=col, font=f)
    return img
files=sorted(glob.glob('frames/*.txt'))
imgs=[(os.path.basename(f)[:-4],render(f)) for f in files]
cols=3; rows=(len(imgs)+cols-1)//cols
W,H=128*S+16, 64*S+30
sheet=Image.new('RGB',(cols*W, rows*H),(30,30,35))
d=ImageDraw.Draw(sheet)
fnt=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',16)
for i,(name,im) in enumerate(imgs):
    cx,cy=(i%cols)*W, (i//cols)*H
    d.text((cx+8,cy+4),name,fill=(200,200,210),font=fnt)
    sheet.paste(im,(cx+8,cy+24))
sheet.save('screens.png')
print('frames:',len(imgs))
