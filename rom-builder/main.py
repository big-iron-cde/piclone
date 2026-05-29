import os

def main():
    # Replace all bytes with NOP
    rom = bytearray([0xEA] * 0x8000)   # 32 KB, not 128 KB

# Program at CPU $8000 (= file offset 0)
    rom[0x0000] = 0xA9   # LDA #$05
    rom[0x0001] = 0x05
    rom[0x0002] = 0x18   # CLC
    rom[0x0003] = 0x69   # ADC #$03
    rom[0x0004] = 0x03
    rom[0x0005] = 0x8D   # STA $4000
    rom[0x0006] = 0x00   # low byte
    rom[0x0007] = 0x40   # high byte
    rom[0x0008] = 0x4C   # JMP $8000
    rom[0x0009] = 0x00
    rom[0x000A] = 0x80

    # Reset vector at $FFFC-$FFFD (file offset $7FFC)
    rom[0x7FFC] = 0x00   # low byte
    rom[0x7FFD] = 0x80   # high byte → CPU starts at $8000

    # Create a ROM binary in the bin/ folder
    os.makedirs("bin", exist_ok = True)
    with open("bin/rom.bin", "wb") as fh:
        fh.write(rom)

if __name__ == "__main__":
    main()