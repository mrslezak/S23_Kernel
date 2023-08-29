/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Chen Zhong <chen.zhong@mediatek.com>
 */

#ifndef __MFD_MT6323_REGISTERS_H__
#define __MFD_MT6323_REGISTERS_H__

/* PMIC Registers */
#define MT6323_CHR_CON0           0x0000
#define MT6323_CHR_CON1           0x0002
#define MT6323_CHR_CON2           0x0004
#define MT6323_CHR_CON3           0x0006
#define MT6323_CHR_CON4           0x0008
#define MT6323_CHR_CON5           0x000A
#define MT6323_CHR_CON6           0x000C
#define MT6323_CHR_CON7           0x000E
#define MT6323_CHR_CON8           0x0010
#define MT6323_CHR_CON9           0x0012
#define MT6323_CHR_CON10          0x0014
#define MT6323_CHR_CON11          0x0016
#define MT6323_CHR_CON12          0x0018
#define MT6323_CHR_CON13          0x001A
#define MT6323_CHR_CON14          0x001C
#define MT6323_CHR_CON15          0x001E
#define MT6323_CHR_CON16          0x0020
#define MT6323_CHR_CON17          0x0022
#define MT6323_CHR_CON18          0x0024
#define MT6323_CHR_CON19          0x0026
#define MT6323_CHR_CON20          0x0028
#define MT6323_CHR_CON21          0x002A
#define MT6323_CHR_CON22          0x002C
#define MT6323_CHR_CON23          0x002E
#define MT6323_CHR_CON24          0x0030
#define MT6323_CHR_CON25          0x0032
#define MT6323_CHR_CON26          0x0034
#define MT6323_CHR_CON27          0x0036
#define MT6323_CHR_CON28          0x0038
#define MT6323_CHR_CON29          0x003A
#define MT6323_STRUP_CON0         0x003C
#define MT6323_STRUP_CON2         0x003E
#define MT6323_STRUP_CON3         0x0040
#define MT6323_STRUP_CON4         0x0042
#define MT6323_STRUP_CON5         0x0044
#define MT6323_STRUP_CON6         0x0046
#define MT6323_STRUP_CON7         0x0048
#define MT6323_STRUP_CON8         0x004A
#define MT6323_STRUP_CON9         0x004C
#define MT6323_STRUP_CON10        0x004E
#define MT6323_STRUP_CON11        0x0050
#define MT6323_SPK_CON0           0x0052
#define MT6323_SPK_CON1           0x0054
#define MT6323_SPK_CON2           0x0056
#define MT6323_SPK_CON6           0x005E
#define MT6323_SPK_CON7           0x0060
#define MT6323_SPK_CON8           0x0062
#define MT6323_SPK_CON9           0x0064
#define MT6323_SPK_CON10          0x0066
#define MT6323_SPK_CON11          0x0068
#define MT6323_SPK_CON12          0x006A
#define MT6323_CID                0x0100
#define MT6323_TOP_CKPDN0         0x0102
#define MT6323_TOP_CKPDN0_SET     0x0104
#define MT6323_TOP_CKPDN0_CLR     0x0106
#define MT6323_TOP_CKPDN1         0x0108
#define MT6323_TOP_CKPDN1_SET     0x010A
#define MT6323_TOP_CKPDN1_CLR     0x010C
#define MT6323_TOP_CKPDN2         0x010E
#define MT6323_TOP_CKPDN2_SET     0x0110
#define MT6323_TOP_CKPDN2_CLR     0x0112
#define MT6323_TOP_RST_CON        0x0114
#define MT6323_TOP_RST_CON_SET    0x0116
#define MT6323_TOP_RST_CON_CLR    0x0118
#define MT6323_TOP_RST_MISC       0x011A
#define MT6323_TOP_RST_MISC_SET   0x011C
#define MT6323_TOP_RST_MISC_CLR   0x011E
#define MT6323_TOP_CKCON0         0x0120
#define MT6323_TOP_CKCON0_SET     0x0122
#define MT6323_TOP_CKCON0_CLR     0x0124
#define MT6323_TOP_CKCON1         0x0126
#define MT6323_TOP_CKCON1_SET     0x0128
#define MT6323_TOP_CKCON1_CLR     0x012A
#define MT6323_TOP_CKTST0         0x012C
#define MT6323_TOP_CKTST1         0x012E
#define MT6323_TOP_CKTST2         0x0130
#define MT6323_TEST_OUT           0x0132
#define MT6323_TEST_CON0          0x0134
#define MT6323_TEST_CON1          0x0136
#define MT6323_EN_STATUS0         0x0138
#define MT6323_EN_STATUS1         0x013A
#define MT6323_OCSTATUS0          0x013C
#define MT6323_OCSTATUS1          0x013E
#define MT6323_PGSTATUS           0x0140
#define MT6323_CHRSTATUS          0x0142
#define MT6323_TDSEL_CON          0x0144
#define MT6323_RDSEL_CON          0x0146
#define MT6323_SMT_CON0           0x0148
#define MT6323_SMT_CON1           0x014A
#define MT6323_SMT_CON2           0x014C
#define MT6323_SMT_CON3           0x014E
#define MT6323_SMT_CON4           0x0150
#define MT6323_DRV_CON0           0x0152
#define MT6323_DRV_CON1           0x0154
#define MT6323_DRV_CON2           0x0156
#define MT6323_DRV_CON3           0x0158
#define MT6323_DRV_CON4           0x015A
#define MT6323_SIMLS1_CON         0x015C
#define MT6323_SIMLS2_CON         0x015E
#define MT6323_INT_CON0           0x0160
#define MT6323_INT_CON0_SET       0x0162
#define MT6323_INT_CON0_CLR       0x0164
#define MT6323_INT_CON1           0x0166
#define MT6323_INT_CON1_SET       0x0168
#define MT6323_INT_CON1_CLR       0x016A
#define MT6323_INT_MISC_CON       0x016C
#define MT6323_INT_MISC_CON_SET   0x016E
#define MT6323_INT_MISC_CON_CLR   0x0170
#define MT6323_INT_STATUS0        0x0172
#define MT6323_INT_STATUS1        0x0174
#define MT6323_OC_GEAR_0          0x0176
#define MT6323_OC_GEAR_1          0x0178
#define MT6323_OC_GEAR_2          0x017A
#define MT6323_OC_CTL_VPROC       0x017C
#define MT6323_OC_CTL_VSYS        0x017E
#define MT6323_OC_CTL_VPA         0x0180
#define MT6323_FQMTR_CON0         0x0182
#define MT6323_FQMTR_CON1         0x0184
#define MT6323_FQMTR_CON2         0x0186
#define MT6323_RG_SPI_CON         0x0188
#define MT6323_DEW_DIO_EN         0x018A
#define MT6323_DEW_READ_TEST      0x018C
#define MT6323_DEW_WRITE_TEST     0x018E
#define MT6323_DEW_CRC_SWRST      0x0190
#define MT6323_DEW_CRC_EN         0x0192
#define MT6323_DEW_CRC_VAL        0x0194
#define MT6323_DEW_DBG_MON_SEL    0x0196
#define MT6323_DEW_CIPHER_KEY_SEL 0x0198
#define MT6323_DEW_CIPHER_IV_SEL  0x019A
#define MT6323_DEW_CIPHER_EN      0x019C
#define MT6323_DEW_CIPHER_RDY     0x019E
#define MT6323_DEW_CIPHER_MODE    0x01A0
#define MT6323_DEW_CIPHER_SWRST   0x01A2
#define MT6323_DEW_RDDMY_NO       0x01A4
#define MT6323_DEW_RDATA_DLY_SEL  0x01A6
#define MT6323_BUCK_CON0          0x0200
#define MT6323_BUCK_CON1          0x0202
#define MT6323_BUCK_CON2          0x0204
#define MT6323_BUCK_CON3          0x0206
#define MT6323_BUCK_CON4          0x0208
#define MT6323_BUCK_CON5          0x020A
#define MT6323_VPROC_CON0         0x020C
#define MT6323_VPROC_CON1         0x020E
#define MT6323_VPROC_CON2         0x0210
#define MT6323_VPROC_CON3         0x0212
#define MT6323_VPROC_CON4         0x0214
#define MT6323_VPROC_CON5         0x0216
#define MT6323_VPROC_CON7         0x021A
#define MT6323_VPROC_CON8         0x021C
#define MT6323_VPROC_CON9         0x021E
#define MT6323_VPROC_CON10        0x0220
#define MT6323_VPROC_CON11        0x0222
#define MT6323_VPROC_CON12        0x0224
#define MT6323_VPROC_CON13        0x0226
#define MT6323_VPROC_CON14        0x0228
#define MT6323_VPROC_CON15        0x022A
#define MT6323_VPROC_CON18        0x0230
#define MT6323_VSYS_CON0          0x0232
#define MT6323_VSYS_CON1          0x0234
#define MT6323_VSYS_CON2          0x0236
#define MT6323_VSYS_CON3          0x0238
#define MT6323_VSYS_CON4          0x023A
#define MT6323_VSYS_CON5          0x023C
#define MT6323_VSYS_CON7          0x0240
#define MT6323_VSYS_CON8          0x0242
#define MT6323_VSYS_CON9          0x0244
#define MT6323_VSYS_CON10         0x0246
#define MT6323_VSYS_CON11         0x0248
#define MT6323_VSYS_CON12         0x024A
#define MT6323_VSYS_CON13         0x024C
#define MT6323_VSYS_CON14         0x024E
#define MT6323_VSYS_CON15         0x0250
#define MT6323_VSYS_CON18         0x0256
#define MT6323_VPA_CON0           0x0300
#define MT6323_VPA_CON1           0x0302
#define MT6323_VPA_CON2           0x0304
#define MT6323_VPA_CON3           0x0306
#define MT6323_VPA_CON4           0x0308
#define MT6323_VPA_CON5           0x030A
#define MT6323_VPA_CON7           0x030E
#define MT6323_VPA_CON8           0x0310
#define MT6323_VPA_CON9           0x0312
#define MT6323_VPA_CON10          0x0314
#define MT6323_VPA_CON11          0x0316
#define MT6323_VPA_CON12          0x0318
#define MT6323_VPA_CON14          0x031C
#define MT6323_VPA_CON16          0x0320
#define MT6323_VPA_CON17          0x0322
#define MT6323_VPA_CON18          0x0324
#define MT6323_VPA_CON19          0x0326
#define MT6323_VPA_CON20          0x0328
#define MT6323_BUCK_K_CON0        0x032A
#define MT6323_BUCK_K_CON1        0x032C
#define MT6323_BUCK_K_CON2        0x032E
#define MT6323_ISINK0_CON0        0x0330
#define MT6323_ISINK0_CON1        0x0332
#define MT6323_ISINK0_CON2        0x0334
#define MT6323_ISINK0_CON3        0x0336
#define MT6323_ISINK1_CON0        0x0338
#define MT6323_ISINK1_CON1        0x033A
#define MT6323_ISINK1_CON2        0x033C
#define MT6323_ISINK1_CON3        0x033E
#define MT6323_ISINK2_CON0        0x0340
#define MT6323_ISINK2_CON1        0x0342
#define MT6323_ISINK2_CON2        0x0344
#define MT6323_ISINK2_CON3        0x0346
#define MT6323_ISINK3_CON0        0x0348
#define MT6323_ISINK3_CON1        0x034A
#define MT6323_ISINK3_CON2        0x034C
#define MT6323_ISINK3_CON3        0x034E
#define MT6323_ISINK_ANA0         0x0350
#define MT6323_ISINK_ANA1         0x0352
#define MT6323_ISINK_PHASE_DLY    0x0354
#define MT6323_ISINK_EN_CTRL      0x0356
#define MT6323_ANALDO_CON0        0x0400
#define MT6323_ANALDO_CON1        0x0402
#define MT6323_ANALDO_CON2        0x0404
#define MT6323_ANALDO_CON3        0x0406
#define MT6323_ANALDO_CON4        0x0408
#define MT6323_ANALDO_CON5        0x040A
#define MT6323_ANALDO_CON6        0x040C
#define MT6323_ANALDO_CON7        0x040E
#define MT6323_ANALDO_CON8        0x0410
#define MT6323_ANALDO_CON10       0x0412
#define MT6323_ANALDO_CON15       0x0414
#define MT6323_ANALDO_CON16       0x0416
#define MT6323_ANALDO_CON17       0x0418
#define MT6323_ANALDO_CON18       0x041A
#define MT6323_ANALDO_CON19       0x041C
#define MT6323_ANALDO_CON20       0x041E
#define MT6323_ANALDO_CON21       0x0420
#define MT6323_DIGLDO_CON0        0x0500
#define MT6323_DIGLDO_CON2        0x0502
#define MT6323_DIGLDO_CON3        0x0504
#define MT6323_DIGLDO_CON5        0x0506
#define MT6323_DIGLDO_CON6        0x0508
#define MT6323_DIGLDO_CON7        0x050A
#define MT6323_DIGLDO_CON8        0x050C
#define MT6323_DIGLDO_CON9        0x050E
#define MT6323_DIGLDO_CON10       0x0510
#define MT6323_DIGLDO_CON11       0x0512
#define MT6323_DIGLDO_CON12       0x0514
#define MT6323_DIGLDO_CON13       0x0516
#define MT6323_DIGLDO_CON14       0x0518
#define MT6323_DIGLDO_CON15       0x051A
#define MT6323_DIGLDO_CON16       0x051C
#define MT6323_DIGLDO_CON17       0x051E
#define MT6323_DIGLDO_CON18       0x0520
#define MT6323_DIGLDO_CON19       0x0522
#define MT6323_DIGLDO_CON20       0x0524
#define MT6323_DIGLDO_CON21       0x0526
#define MT6323_DIGLDO_CON23       0x0528
#define MT6323_DIGLDO_CON24       0x052A
#define MT6323_DIGLDO_CON26       0x052C
#define MT6323_DIGLDO_CON27       0x052E
#define MT6323_DIGLDO_CON28       0x0530
#define MT6323_DIGLDO_CON29       0x0532
#define MT6323_DIGLDO_CON30       0x0534
#define MT6323_DIGLDO_CON31       0x0536
#define MT6323_DIGLDO_CON32       0x0538
#define MT6323_DIGLDO_CON33       0x053A
#define MT6323_DIGLDO_CON34       0x053C
#define MT6323_DIGLDO_CON35       0x053E
#define MT6323_DIGLDO_CON36       0x0540
#define MT6323_DIGLDO_CON39       0x0542
#define MT6323_DIGLDO_CON40       0x0544
#define MT6323_DIGLDO_CON41       0x0546
#define MT6323_DIGLDO_CON42       0x0548
#define MT6323_DIGLDO_CON43       0x054A
#define MT6323_DIGLDO_CON44       0x054C
#define MT6323_DIGLDO_CON45       0x054E
#define MT6323_DIGLDO_CON46       0x0550
#define MT6323_DIGLDO_CON47       0x0552
#define MT6323_DIGLDO_CON48       0x0554
#define MT6323_DIGLDO_CON49       0x0556
#define MT6323_DIGLDO_CON50       0x0558
#define MT6323_DIGLDO_CON51       0x055A
#define MT6323_DIGLDO_CON52       0x055C
#define MT6323_DIGLDO_CON53       0x055E
#define MT6323_DIGLDO_CON54       0x0560
#define MT6323_EFUSE_CON0         0x0600
#define MT6323_EFUSE_CON1         0x0602
#define MT6323_EFUSE_CON2         0x0604
#define MT6323_EFUSE_CON3         0x0606
#define MT6323_EFUSE_CON4         0x0608
#define MT6323_EFUSE_CON5         0x060A
#define MT6323_EFUSE_CON6         0x060C
#define MT6323_EFUSE_VAL_0_15     0x060E
#define MT6323_EFUSE_VAL_16_31    0x0610
#define MT6323_EFUSE_VAL_32_47    0x0612
#define MT6323_EFUSE_VAL_48_63    0x0614
#define MT6323_EFUSE_VAL_64_79    0x0616
#define MT6323_EFUSE_VAL_80_95    0x0618
#define MT6323_EFUSE_VAL_96_111   0x061A
#define MT6323_EFUSE_VAL_112_127  0x061C
#define MT6323_EFUSE_VAL_128_143  0x061E
#define MT6323_EFUSE_VAL_144_159  0x0620
#define MT6323_EFUSE_VAL_160_175  0x0622
#define MT6323_EFUSE_VAL_176_191  0x0624
#define MT6323_EFUSE_DOUT_0_15    0x0626
#define MT6323_EFUSE_DOUT_16_31   0x0628
#define MT6323_EFUSE_DOUT_32_47   0x062A
#define MT6323_EFUSE_DOUT_48_63   0x062C
#define MT6323_EFUSE_DOUT_64_79   0x062E
#define MT6323_EFUSE_DOUT_80_95   0x0630
#define MT6323_EFUSE_DOUT_96_111  0x0632
#define MT6323_EFUSE_DOUT_112_127 0x0634
#define MT6323_EFUSE_DOUT_128_143 0x0636
#define MT6323_EFUSE_DOUT_144_159 0x0638
#define MT6323_EFUSE_DOUT_160_175 0x063A
#define MT6323_EFUSE_DOUT_176_191 0x063C
#define MT6323_EFUSE_CON7         0x063E
#define MT6323_EFUSE_CON8         0x0640
#define MT6323_EFUSE_CON9         0x0642
#define MT6323_RTC_MIX_CON0       0x0644
#define MT6323_RTC_MIX_CON1       0x0646
#define MT6323_AUDTOP_CON0        0x0700
#define MT6323_AUDTOP_CON1        0x0702
#define MT6323_AUDTOP_CON2        0x0704
#define MT6323_AUDTOP_CON3        0x0706
#define MT6323_AUDTOP_CON4        0x0708
#define MT6323_AUDTOP_CON5        0x070A
#define MT6323_AUDTOP_CON6        0x070C
#define MT6323_AUDTOP_CON7        0x070E
#define MT6323_AUDTOP_CON8        0x0710
#define MT6323_AUDTOP_CON9        0x0712
#define MT6323_AUXADC_ADC0        0x0714
#define MT6323_AUXADC_ADC1        0x0716
#define MT6323_AUXADC_ADC2        0x0718
#define MT6323_AUXADC_ADC3        0x071A
#define MT6323_AUXADC_ADC4        0x071C
#define MT6323_AUXADC_ADC5        0x071E
#define MT6323_AUXADC_ADC6        0x0720
#define MT6323_AUXADC_ADC7        0x0722
#define MT6323_AUXADC_ADC8        0x0724
#define MT6323_AUXADC_ADC9        0x0726
#define MT6323_AUXADC_ADC10       0x0728
#define MT6323_AUXADC_ADC11       0x072A
#define MT6323_AUXADC_ADC12       0x072C
#define MT6323_AUXADC_ADC13       0x072E
#define MT6323_AUXADC_ADC14       0x0730
#define MT6323_AUXADC_ADC15       0x0732
#define MT6323_AUXADC_ADC16       0x0734
#define MT6323_AUXADC_ADC17       0x0736
#define MT6323_AUXADC_ADC18       0x0738
#define MT6323_AUXADC_ADC19       0x073A
#define MT6323_AUXADC_ADC20       0x073C
#define MT6323_AUXADC_RSV1        0x073E
#define MT6323_AUXADC_RSV2        0x0740
#define MT6323_AUXADC_CON0        0x0742
#define MT6323_AUXADC_CON1        0x0744
#define MT6323_AUXADC_CON2        0x0746
#define MT6323_AUXADC_CON3        0x0748
#define MT6323_AUXADC_CON4        0x074A
#define MT6323_AUXADC_CON5        0x074C
#define MT6323_AUXADC_CON6        0x074E
#define MT6323_AUXADC_CON7        0x0750
#define MT6323_AUXADC_CON8        0x0752
#define MT6323_AUXADC_CON9        0x0754
#define MT6323_AUXADC_CON10       0x0756
#define MT6323_AUXADC_CON11       0x0758
#define MT6323_AUXADC_CON12       0x075A
#define MT6323_AUXADC_CON13       0x075C
#define MT6323_AUXADC_CON14       0x075E
#define MT6323_AUXADC_CON15       0x0760
#define MT6323_AUXADC_CON16       0x0762
#define MT6323_AUXADC_CON17       0x0764
#define MT6323_AUXADC_CON18       0x0766
#define MT6323_AUXADC_CON19       0x0768
#define MT6323_AUXADC_CON20       0x076A
#define MT6323_AUXADC_CON21       0x076C
#define MT6323_AUXADC_CON22       0x076E
#define MT6323_AUXADC_CON23       0x0770
#define MT6323_AUXADC_CON24       0x0772
#define MT6323_AUXADC_CON25       0x0774
#define MT6323_AUXADC_CON26       0x0776
#define MT6323_AUXADC_CON27       0x0778
#define MT6323_ACCDET_CON0        0x077A
#define MT6323_ACCDET_CON1        0x077C
#define MT6323_ACCDET_CON2        0x077E
#define MT6323_ACCDET_CON3        0x0780
#define MT6323_ACCDET_CON4        0x0782
#define MT6323_ACCDET_CON5        0x0784
#define MT6323_ACCDET_CON6        0x0786
#define MT6323_ACCDET_CON7        0x0788
#define MT6323_ACCDET_CON8        0x078A
#define MT6323_ACCDET_CON9        0x078C
#define MT6323_ACCDET_CON10       0x078E
#define MT6323_ACCDET_CON11       0x0790
#define MT6323_ACCDET_CON12       0x0792
#define MT6323_ACCDET_CON13       0x0794
#define MT6323_ACCDET_CON14       0x0796
#define MT6323_ACCDET_CON15       0x0798
#define MT6323_ACCDET_CON16       0x079A

#endif /* __MFD_MT6323_REGISTERS_H__ */