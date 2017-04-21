clear all
clc
close all

a = fopen('test.txt', 'r');
b = fread(a);
c = char(b);
c = c';
pH = [];
o2 = [];
pHInd = [];
o2Ind = [];
stars = 0;
dash = 0;
for i = 1:length(c)
    if c(i) ~= '*' && stars == 1
        pHInd = [pHInd c(i)];
    end
    if c(i) == '*' && stars == 0
        stars = 1;
    elseif c(i) == '*' && stars == 1
        pH = [pH str2num(pHInd)];
        pHInd = [];
        stars = 0;
    end
    if c(i) ~= '-' && dash == 1
        o2Ind = [o2Ind c(i)];
    end
    if c(i) == '-' && dash == 0
        dash = 1;
    elseif c(i) == '-' && dash == 1
        o2 = [o2 str2num(o2Ind)];
        o2Ind = [];
        dash = 0;
    end
end
xvec = 1:length(o2);
plot(xvec, pH)
hold on
plot(xvec, o2)
hold off
title('pH and Dissolved Oxygen Over Time')
xlabel('samples')
legend('pH', 'o2')
