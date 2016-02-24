FATE_INDEO2-$(call DEMDEC, AVI, INDEO2) += fate-indeo2-intra
fate-indeo2-intra: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/VPAR0026.AVI

FATE_INDEO2-$(call DEMDEC, AVI, INDEO2) += fate-indeo2-delta
fate-indeo2-delta: CMD = framecrc -i $(TARGET_SAMPLES)/rt21/ISKATE.AVI -an

FATE_SAMPLES_AVCONV += $(FATE_INDEO2-yes)
fate-indeo2: $(FATE_INDEO2-yes)

FATE_INDEO-$(call DEMDEC, MOV, INDEO3) += fate-indeo3
fate-indeo3: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/cubes.mov

FATE_INDEO-$(call DEMDEC, AVI, INDEO3) += fate-indeo3-2
fate-indeo3-2: CMD = framecrc -i $(TARGET_SAMPLES)/iv32/OPENINGH.avi

FATE_INDEO-$(call DEMDEC, AVI, INDEO4) += fate-indeo4
fate-indeo4: CMD = framecrc -i $(TARGET_SAMPLES)/iv41/indeo41-partial.avi -an

FATE_INDEO-$(call DEMDEC, AVI, INDEO5) += fate-indeo5
fate-indeo5: CMD = framecrc -i $(TARGET_SAMPLES)/iv50/Educ_Movie_DeadlyForce.avi -an

FATE_SAMPLES_AVCONV += $(FATE_INDEO-yes)
fate-indeo: fate-indeo2 $(FATE_INDEO-yes)
