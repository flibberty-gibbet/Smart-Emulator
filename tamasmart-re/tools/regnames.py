import re,sys,json
src=open(sys.argv[1]).read().split('\n')[1326:1782]
names={}
for l in src:
    m=re.search(r'//\s*([0-9a-f]{4})\s*-\s*([A-Za-z0-9_ ]+?)\s*(\(|$|or |-)',l)
    if m: names[int(m.group(1),16)]=m.group(2).strip().replace(' ','_'); continue
    m=re.search(r'map\(0x00([0-9a-f]{4}), 0x00([0-9a-f]{4})\).*?//\s*(?:[0-9a-f]{4}\s*)?([A-Za-z][A-Za-z0-9_ ]*)',l)
    if m and int(m.group(1),16)==int(m.group(2),16): names[int(m.group(1),16)]=m.group(3).strip().replace(' ','_')
ports='ABCDEF'; sub=['Data','Buffer','Dir','Attrib','Drv','Mux','Latch','KeyEn']
for p in range(6):
    for s in range(8): names[0x7860+p*8+s]='IO%s_%s'%(ports[p],sub[s])
for a in range(0x7000,0x7008): names.setdefault(a,'Tx3_reg%d'%(a-0x7000))
for a in range(0x7010,0x7016): names.setdefault(a,'Tx1_reg%d'%(a-0x7010))
for a in range(0x7016,0x701c): names.setdefault(a,'Tx2_reg%d'%(a-0x7016))
for a in range(0x7a80,0x7a88): names[a]='DMA0_reg%d'%(a-0x7a80)
for a in range(0x7a88,0x7a90): names[a]='DMA1_reg%d'%(a-0x7a88)
names[0x7809]='PM_Ctrl'; names[0x780a]='Watchdog_Ctrl'; names[0x780b]='Watchdog_Clear'
names[0x7b40]='SPIFC_Ctrl1';names[0x7b41]='SPIFC_CMD';names[0x7b42]='SPIFC_PARA';names[0x7b46]='SPIFC_RX_Data';names[0x7b47]='SPIFC_TX_BC';names[0x7b48]='SPIFC_RX_BC';names[0x7b49]='SPIFC_TIMING'
names[0x79f4]='RTC_Ready';names[0x79f5]='RTC_ReadData';names[0x7af0]='Byte_Swap';names[0x78e8]='TimerH_Ctrl'
json.dump({('%04x'%k):v for k,v in sorted(names.items())},open('analysis/regnames.json','w'),indent=0)
print(len(names))
