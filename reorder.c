#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 生成10000个随机数
int* random_numbers() {
    int num = 10000;
    int *a = (int *)malloc(num * sizeof(int));
    if(a == NULL) {
        printf("Memory allocation failed!\n");
        return NULL;
    }
    srand(time(NULL));
    for(int i = 0; i < num; i++)
        a[i] = rand();
    return a;
}

void selection_sort(int *arr) {
    int num = 1000;
    for(int i = 0; i < num - 1; i++) {
        int min_index = i;
        for(int j = i + 1; j < num; j++)
            if(arr[j] < arr[min_index])
                min_index = j;
        int tmp = arr[i];
        arr[i] = arr[min_index];
        arr[min_index] = tmp;
    }
}

void bubble_sort(int *arr) {
    int num = 10000;
    for(int i = 0; i < num - 1; i++)
        for(int j = 0; j < num - i - 1; j++)
            if(arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
}

int main() {
    for(int a = 0; a < 1000; a++) {
        int *random_array = random_numbers();
        if(random_array == NULL)
            return 0;
        // 排序
        selection_sort(random_array);
        bubble_sort(random_array);
        if(a == 999) {
            for(int i = 0; i < 10; i++)
                printf("%d ", random_array[i]);
            for(int i = 1000; i < 1010; i++)
                printf("%d ", random_array[i]);
            printf("\n");
        }
        free(random_array);
    }
    return 0;
}
