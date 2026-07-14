clear;
clc;
close all;


%% Load CSV

data = readmatrix("metrics_log.txt");


sec = data(:,1);
nsec = data(:,2);

commit = data(:,3);
identity = data(:,4);
account = data(:,5);
info = data(:,6);

buffer = data(:,7);
cpu = data(:,8);

%% Convert Unix + nanoseconds to seconds

time = sec + nsec*1e-9;


%% ==========================
% JITTER CALCULATION
% ==========================

period = 1;   % logger period = 1 second


% Time between executions

dt = diff(time);


% Jitter = measured period - ideal period

jitter_ms = (dt - period)*1000;



% Time axis for jitter

jitter_time = time(2:end) - time(1);



%% ==========================
% Plot Jitter
% ==========================

figure;

plot(jitter_time, jitter_ms, 'LineWidth', 1);

grid on;
yline(0,'--');

xlabel("Time (s)");
ylabel("Jitter (ms)");

title("Periodic Thread Execution Jitter");



%% ==========================
% Statistics
% ==========================

fprintf("Max jitter : %.4f ms\n", max(jitter_ms));
fprintf("Min jitter : %.4f ms\n", min(jitter_ms));

%% ==========================
% 2) MESSAGE RATE + BUFFER
% ===========================


% Replace NaN with zero
commit(isnan(commit)) = 0;
identity(isnan(identity)) = 0;
account(isnan(account)) = 0;
info(isnan(info)) = 0;


% Total messages received
total_messages = commit + identity + account + info;


% Messages per second
msg_rate = max(diff(total_messages),0);
msg_time = time(2:end);


figure;


yyaxis left

plot(msg_time,msg_rate,'LineWidth',1);

ylabel("Incoming messages (Hz)");



yyaxis right

plot(time,buffer,'LineWidth',1);

ylabel("Buffer occupancy (%)");


xlabel("Time (s)");

title("Network Load and Circular Buffer Usage");

grid on;



%% ==========================
% 3) CPU LOAD
% ===========================


cpu_idle = 100 - cpu;


figure;


yyaxis left

plot(msg_time,msg_rate,'LineWidth',1);

ylabel("Incoming messages (Hz)");



yyaxis right

plot(time,cpu_idle,'LineWidth',1);

ylabel("CPU Idle (%)");


xlabel("Time (s)");

title("CPU Utilization vs Network Traffic");

grid on;