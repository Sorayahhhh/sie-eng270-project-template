#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double min(double a, double b) {
    return (a < b) ? a : b;
}

double max(double a, double b) {
    return (a > b) ? a : b;
}

int readFile(char * filename, double * table, int length) {
    // Open
    FILE * file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Impossible to open the file '%s'\n", filename);
        exit(1);
    }

    // Read line by line
    int n = 0;
    char buffer[100];
    while (fgets(buffer, 100, file) != NULL) {
        if (n >= length) break;
        table[n] = atof(buffer);
        n++;
    }

    // Close file, return number of lines
    fclose(file);
    return n;
}

struct Daily {
    int     day;
    double  C;                      // Storage Capacity           
    double  V_in;                   // Daily incoming volume (before demand, water loss due to RC, f_m, overflow etc.)
    double  V_served;               // Served volume
    double  V_stored;               // Storage "at the end of the day", i.e. after demand and water loss
    double  O_loss;                 // Water loss due to overflow
    int     D_met;                  // 1 if demand fully met, else 0
    int     D_failed;               // 1 if NO water was served, else 0
};

struct Reliability {
    int     days_tot;               // Number of days
    double  C;                      // Storage Capacity  
    double  temp_rel;               // Temporal reliability
    double  vol_rel;                // Volumetric reliability
    double  o_rel;                  // Fraction of inflowing water lost due to overflow events
    double  failure;                // fraction of days where no water was served
    int     drought_max;            // Longest period with no rainfall
};

struct Supply {
    int     days_tot;               // Number of days
    double  C;                      // Storage Capacity  
    int     failed_max;             // Max. number of consecutive days with no water served
    double  failed_average;         // Average number of consecutive days with no water served

    int     unmet_max;              // Max. number of consecutive days with unmet demand
    double  unmet_average;          // Average number of consecutive days with unmet demand

    int     met_max;                // Max. number of consecutive days where demand was met
    double  met_average;            // Average number of consecutive days where demand was met
};

struct Overflow{
    int     days_tot;               // Number of days
    double  C;                      // Storage Capacity  
    int     ov_days_max;            // Maximal number of consecutive days with overflow
    double  ov_day_average;         // Average number of consecutive days with overflow
    int     ov_max;                 // Max. water loss in an overflow event
    double  ov_average;             // Average water loss in an overflow event
};

void waterbalance(struct Daily *d, double V0, double P_d, double A_r, double RC, double FF, double f_m, double D, double C){
    // This function calculates the net daily storage volume:

    // P_d  daily precipitation                 [mm]
    // A_r  Mean roof area per capita           [m^2/capita] 
    // RC   Mean runoff coefficient
    // FF   First flush diversion               [mm]
    // f_m  Water loss due to (mesh) filter
    // D    Mean daily water demand per capita  [m^3/capita] 
    // C    Storage capacity per capita         [m^3/capita]
    // V0   Volume already present              [m^3/capita]

    d->C = C;

    // 1) Total daily runoff collected from the roof:
    double V_tot = P_d/1000 * A_r * RC;

    // 2) First flush diversion:
    double V_FF = FF/1000 * A_r;

    // 3) Incoming volume:
    double V_in = 0;
    if (V_tot >= V_FF) {
        V_in = (1-f_m)*(V_tot - V_FF);
    }

    d->V_in = V_in;

    // 4) Available volume:
    double V_available = V0 + V_in;

    // 5) Daily served water quantity:
    double V_served = min(V_available, D);
    d->V_served = V_served;

    if (V_served == D){             
        d->D_met = 1;
        d->D_failed = 0;
    } else {
        d->D_met = 0;
        if (V_served == 0) {
            d->D_failed = 1;
        } else {
            d->D_failed = 0;
        }
    }

    // 6) Daily overflow:
    double O_loss = max(0, V_available - V_served - C);
    d->O_loss = O_loss;

    // 7) Storage at the end of the day:
    if (O_loss > 0){
        d->V_stored = C;
    } else {
        d->V_stored = V_available - V_served;
    }
}

int main (int argc, char * argv[]) {
    // Precipitation data
    double precipitation[10000];
    char* dataset = "sydney_clean.csv";
    int length = readFile(dataset, precipitation, 10000);

    // constants: Evaluation (Sydney, AU):
    double  A_r = 50; 
    double  RC = 0.85;
    double  FF = 0.75;
    double  f_m = 0.1;
    double  D = 0.0395;

    // constants: Analysis (Lahore, PK):
    // double  A_r = 50; 
    // double  RC = 0.85;
    // double  FF = 0.75;
    // double  f_m = 0.1;
    // double  D = 0.0395;

    // Initializing variables (for function water balance)
    double  P_d = 0;
    int     t = 0; 
    double  tanksize = 0.0;            
    struct  Daily daily[length];

    
    // Create malloc table for storage capacity C:
    double  start = 0.25;                               // data AU (Evaluation)
    double  end = 35;                                   // data AU (Evaluation)
    double  step = 0.05;
    int     range = (int)((end - start)/step) + 1;

    double *C = (double *)malloc(range * sizeof(double));
    struct Reliability *reliability = malloc(range * sizeof *reliability);
    struct Supply *supply = malloc(range * sizeof *supply);
    struct Overflow *overflow = malloc(range * sizeof *overflow);

    if (C == NULL  || reliability == NULL  || supply == NULL  || overflow == NULL) {
        printf("Memory allocation failed.\n");
        return 1;
    }
    
    for (int k = 0; k < range; k++) {
        C[k] = start + k * step;
    }

    FILE *file1;
    file1 = fopen("daily_results", "w+");
    fprintf(file1, "Data from: %s\n", dataset);
    fprintf(file1,"Day, Tanksize, Storage Volume, Served Volume, Overflow Volume\n");
    
    // Initializing variables
    int drought = 0;                    // days with no rainfall
    int drought_max = 0;                // longest period with no rainfall

    // Check for longest period with no rainfall (only once per dataset)
    for (int k = 0; k < length; k++){

            if (precipitation[k] == 0.0) {
                drought++;
                if (drought > drought_max){
                    drought_max = drought;
                } 
            } else {
                    drought = 0;
                }
            }

    for (int j = 0; j < range; j++) {

        tanksize = C[j];

        // Initializing variables (for analysis)
        double  V0 = 0.0; 

        int     O_count = 0;            // Number of days where water was lost due to overflow 
        int     O_ct_tot = 0;           // Total number of consecutive days with overflow 
        int     O_ct_max = 0;           // Max. number of consecutive days with overflow 
        double  O_vol_sum = 0.0;        // Total volume lost per event
        double  O_vol_max = 0.0;        // Max. volume lost in one event   
        double  O_tot = 0.0;            // Total overflow volume
        int     O_event = 0;            // Number of overflow events
        
        int     D_failed = 0;           // Number of days where no water was provided  
        int     D_failed_max = 0;       // Max. number of consecutive days where no water was provided    
        int     D_failed_sum = 0;       // Total. number of days where no water was provided   
        int     failed_event = 0;       // Period of demand failed
        double  failure = 0.0;          // Fraction of days where no water was provided

        int     D_unmet = 0;            // Number of consecutive days where demand was not fully met
        int     D_unmet_sum = 0;        // Total number of consecutive days where demand was not fully met
        int     D_unmet_max = 0;        // Maximal number of consecutive days where demand was not fully met
        int     unmet_event = 0;        // Period of demand not fully met

        int     D_met = 0;              // Number of consecutive days where demand was fully met
        int     D_met_sum = 0;          // Total number of days where demand was fully met
        int     D_met_max = 0;          // Max. number of consecutive days where demand was met        
        int     met_event = 0;          // Period of demand fully met

        double  V_in_tot = 0.0;         // Total incoming volume
        double  V_served = 0.0;         // Total volume served
        double  D_tot = 0.0;            // Total demand

        double  temp_rel = 0.0;         // Temporal reliability: Fraction of days where demand was fully met
        double  vol_rel = 0.0;          // Volumetric reliability: Fraction of demand covered by RWH
        double  ov_frac = 0.0;          // Fraction of inflowing water lost due to overflow events

        for (int i = 0; i < length; i++){
            t = i + 1;
            daily[i].day = t;
            P_d = precipitation[i];

            waterbalance(&daily[i], V0, P_d, A_r, RC, FF, f_m, D, C[j]);

            // Writing daily results to csv file
            double C_selected[] = {0.5, 1, 2.5, 5, 7.5, 10};
            for (int k = 0; k < 6; k++) {
                if (C[j] == C_selected[k])
                    fprintf(file1,"%.d, %.2f, %.2f, %.2f, %.2f\n", 
                        daily[i].day,
                        daily[i].C, 
                        daily[i].V_stored,
                        daily[i].V_served,
                        daily[i].O_loss);
                }

            // Initial storage for following day:
            V0 = daily[i].V_stored;

            // For temporal reliability:
            D_met_sum += daily[i].D_met;

            // For volumetric reliability:
            V_served += daily[i].V_served;
        
            // Incoming volume and overflow for overflow analysis:
            O_tot += daily[i].O_loss;
            V_in_tot += daily[i].V_in;

            // Water supply analysis (for consecutive days/events)
            if (daily[i].V_served < D) {
                D_met = 0;
                D_unmet++;
                D_unmet_sum++;

                if (daily[i].D_failed == 1){
                    D_failed++;
                    D_failed_sum++;
                    if (D_failed > D_failed_max) {
                        D_failed_max = D_failed;
                    }
                    if (D_failed == 1) {
                    failed_event++;
                    }
                } else {
                    D_failed = 0;
                }

                if (D_unmet > D_unmet_max) {
                    D_unmet_max = D_unmet;
                }

                if (D_unmet == 1) {
                    unmet_event++;
                }
            
            } else {
                D_unmet = 0;
                D_met++;
                if (D_met > D_met_max) {
                    D_met_max = D_met;
                }
                if (D_met == 1) {
                    met_event++;
                }
            }

            // Overflow analysis (for consecutive days)
            if (daily[i].O_loss > 0) {
                O_count++;
                O_ct_tot++;
                O_vol_sum += daily[i].O_loss;
                if (O_count > O_ct_max) {
                    O_ct_max = O_count;
                }
                if (O_count == 1) {
                    O_event++;
                }
                if (O_vol_sum > O_vol_max) {
                    O_vol_max = O_vol_sum;
                }
            } else {
                O_count = 0;
                O_vol_sum = 0.0;
            }
        }

        reliability[j].drought_max = drought_max;
        reliability[j].days_tot = length;

        reliability[j].C = tanksize;
        supply[j].C = tanksize;
        overflow[j].C = tanksize;

        // Overflow analysis (for consecutive days)
        overflow[j].ov_days_max = O_ct_max;
        overflow[j].ov_day_average = (double)O_ct_tot/(double)O_event;
        overflow[j].ov_max = O_vol_max;
        overflow[j].ov_average = (double)O_tot/(double)O_event;

        // Total failure analysis (for consecutive days)
        failure = (double)D_failed_sum/(double)length;
        reliability[j].failure = failure;

        supply[j].failed_max = D_failed_max;

        if (failed_event > 0){
            supply[j].failed_average = (double)D_failed_sum/(double)failed_event;
        } else {
            supply[j].failed_average = 0;
        }

        // Partial failure analysis (for consecutive days)
        supply[j].unmet_max = D_unmet_max;

        if (unmet_event > 0){
            supply[j].unmet_average = (double)D_unmet_sum/(double)unmet_event;
        } else {
            supply[j].unmet_average = 0;
        }

        // Success analysis (for consecutive days)
        supply[j].met_max = D_met_max;

        if (met_event > 0){
            supply[j].met_average = (double)D_met_sum/(double)met_event;
        } else {
            supply[j].met_average = 0;
        }

        // Temporal reliability:
        temp_rel = (double)D_met_sum/(double)length;
        reliability[j].temp_rel = temp_rel;
        // Test: printf("Temporal reliability for storage volume %.2f: %.3f\n", C[j], temp_rel);

        // Volumetric reliability:
        D_tot = D*length;
        vol_rel = (double)V_served/(double)D_tot;
        reliability[j].vol_rel = vol_rel;
        // Test: printf("Volumetric reliability for storage volume %.2f: %.3f\n", C[j], vol_rel);

        // Overflow analysis
        if (V_in_tot > 0) {
            ov_frac = (double)O_tot/(double)V_in_tot;
        } else { 
            ov_frac = 0.0;
        }
        reliability[j].o_rel = ov_frac;
        // Test: printf("Overflow fraction for storage volume %.2f: %.3f\n", C[j], O_rel); 
        
        
    }
    free(C);
    fclose(file1);

    // Test for first 5 days: 
    for (int i = 0; i < 5; i++) {
        printf("Day %d: served = %.3f, overflow =%.3f, stored=%.3f [m^3]\n",
               daily[i].day,
               daily[i].V_served,
               daily[i].O_loss,
               daily[i].V_stored);
    }

   // Test for overall results:
    printf("Results for dataset %s\n", dataset);
    for (int i = 0; i < 3; i++) {
        printf("For storage capacity %0.3f with longest dry period %d\n", reliability[i].C, drought_max);
        printf("RELIABILITY:\nTemporal: %.3f | Volumetric: %.3f | Overflow fraction: %.3f | Total failure fraction: %0.3f |\n",
                reliability[i].C,
                reliability[i].temp_rel,
                reliability[i].vol_rel,
                reliability[i].o_rel,
                reliability[i].failure);
        printf("SUPPLY:\nDays failed (max): %d | Days failed (average): %.2f | Days unmet (max): %d | Days unmet (average): %.2f | Days met (max): %d | Days met (average): %.2f\n",
                supply[i].failed_max,
                supply[i].failed_average,
                supply[i].unmet_max,
                supply[i].unmet_average,
                supply[i].met_max,
                supply[i].met_average);
        printf("OVERFLOW:\nDays (max): %d | Days (average): %.3f | Volume (max): %.3f | Volume (average): %0.3f\n",
                overflow[i].ov_days_max,
                overflow[i].ov_day_average,
                overflow[i].ov_max,
                overflow[i].ov_average);
        printf("\n");
    }

    // Write reliability results to csv file
    FILE *file2;

    file2 = fopen("reliability_results", "w+");
    fprintf(file2, "Data from: %s\n", dataset);
    fprintf(file2, "Longest dry period: %d\n", drought_max);
    fprintf(file2,"Storage capacity, Temporal reliability, Volumetric reliability, Overflow fraction, Total failure (fraction)\n");
    for (int i = 0; i < range; i++) {
        fprintf(file2,"%.3f, %.3f, %.3f, %.3f, %.3f\n", 
            reliability[i].C, 
            reliability[i].temp_rel,
            reliability[i].vol_rel,
            reliability[i].o_rel,
            reliability[i].failure);
    }
    fclose(file2);

    // Write results of supply to csv file
    FILE *file3;

    file3 = fopen("supply_results", "w+");
    fprintf(file3, "Data from %s\n", dataset);
    fprintf(file3,"Storage capacity, failed (max), failed (average), unmet (max), unmet (average), met (max), met (average)\n");
    for (int i = 0; i < range; i++) {
        fprintf(file2,"%.2f, %.d, %.2f, %.d, %.2f, %d, %.2f\n", 
            supply[i].C, 
            supply[i].failed_max,
            supply[i].failed_average,
            supply[i].unmet_max,
            supply[i].unmet_average,
            supply[i].met_max,
            supply[i].met_average);
    }
    fclose(file3);

    // Write results of supply to csv file
    FILE *file4;

    file4 = fopen("overflow_results", "w+");
    fprintf(file4, "Data from %s\n", dataset);
    fprintf(file4,"Storage capacity, Streak (max), Streak (average), Volume (max), Volume (average)\n");
    for (int i = 0; i < range; i++) {
        fprintf(file2,"%.2f, %d, %.2f, %.2f, %.2f\n", 
            overflow[i].C,
            overflow[i].ov_days_max,
            overflow[i].ov_day_average,
            overflow[i].ov_max,
            overflow[i].ov_average);
    }
    fclose(file4);


    return 0;
}