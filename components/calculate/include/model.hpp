#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <string.h>
class Model {
    public:
void add_vectors(double *v1, double *v2, int size, double *result) {
    for(int i = 0; i < size; ++i)
        result[i] = v1[i] + v2[i];
}
void mul_vector_number(double *v1, double num, int size, double *result) {
    for(int i = 0; i < size; ++i)
        result[i] = v1[i] * num;
}
void score(double * input, double * output) {
    double var0[2];
    double var1[2];
    double var2[2];
    double var3[2];
    double var4[2];
    double var5[2];
    double var6[2];
    double var7[2];
    double var8[2];
    double var9[2];
    double var10[2];
    double var11[2];
    double var12[2];
    double var13[2];
    double var14[2];
    double var15[2];
    double var16[2];
    double var17[2];
    double var18[2];
    double var19[2];
    double var20[2];
    if (input[85] <= -22.421420097351074) {
        memcpy(var20, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var20, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    double var21[2];
    if (input[58] <= -18.555493354797363) {
        memcpy(var21, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var21, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var20, var21, 2, var19);
    double var22[2];
    if (input[44] <= -16.279968202114105) {
        memcpy(var22, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var22, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var19, var22, 2, var18);
    double var23[2];
    if (input[34] <= -21.505109310150146) {
        memcpy(var23, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var23, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var18, var23, 2, var17);
    double var24[2];
    if (input[31] <= -18.71264624595642) {
        memcpy(var24, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var24, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var17, var24, 2, var16);
    double var25[2];
    if (input[111] <= -20.021756172180176) {
        memcpy(var25, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var25, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var16, var25, 2, var15);
    double var26[2];
    if (input[80] <= -22.555580615997314) {
        memcpy(var26, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var26, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var15, var26, 2, var14);
    double var27[2];
    if (input[72] <= -15.816365718841553) {
        memcpy(var27, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var27, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var14, var27, 2, var13);
    double var28[2];
    if (input[76] <= -13.398764491081238) {
        memcpy(var28, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var28, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var13, var28, 2, var12);
    double var29[2];
    if (input[45] <= -16.634453553706408) {
        memcpy(var29, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var29, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var12, var29, 2, var11);
    double var30[2];
    if (input[98] <= -25.84164524078369) {
        memcpy(var30, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var30, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var11, var30, 2, var10);
    double var31[2];
    if (input[73] <= -21.923270225524902) {
        memcpy(var31, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var31, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var10, var31, 2, var9);
    double var32[2];
    if (input[90] <= -19.065157890319824) {
        memcpy(var32, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var32, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var9, var32, 2, var8);
    double var33[2];
    if (input[91] <= -25.59650421142578) {
        memcpy(var33, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var33, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var8, var33, 2, var7);
    double var34[2];
    if (input[99] <= -15.895127296447754) {
        memcpy(var34, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var34, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var7, var34, 2, var6);
    double var35[2];
    if (input[122] <= -26.664206504821777) {
        memcpy(var35, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var35, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var6, var35, 2, var5);
    double var36[2];
    if (input[69] <= -20.266459465026855) {
        memcpy(var36, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var36, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var5, var36, 2, var4);
    double var37[2];
    if (input[38] <= -14.741360187530518) {
        memcpy(var37, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var37, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var4, var37, 2, var3);
    double var38[2];
    if (input[98] <= -21.335724353790283) {
        memcpy(var38, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var38, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var3, var38, 2, var2);
    double var39[2];
    if (input[52] <= -15.307774722576141) {
        memcpy(var39, (double[]){1.0, 0.0}, 2 * sizeof(double));
    } else {
        memcpy(var39, (double[]){0.0, 1.0}, 2 * sizeof(double));
    }
    add_vectors(var2, var39, 2, var1);
    mul_vector_number(var1, 0.05, 2, var0);
    memcpy(output, var0, 2 * sizeof(double));
}

};

#endif // AI_MODEL_H