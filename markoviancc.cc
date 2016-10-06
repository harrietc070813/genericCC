#undef NDEBUG // We want the assert statements to work

#include "markoviancc.hh"

#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>

using namespace std;

double MarkovianCC::current_timestamp( void ){
	#ifdef SIMULATION_MODE
	return cur_tick;

	#else
	using namespace std::chrono;
	high_resolution_clock::time_point cur_time_point = \
		high_resolution_clock::now();
	return duration_cast<duration<double>>(cur_time_point - start_time_point)\
		.count()*1000;
	#endif
}

void MarkovianCC::init() {
	if (num_pkts_acked != 0)
		cout << "% Packets Lost: " << (100.0 * num_pkts_lost) /
			(num_pkts_acked + num_pkts_lost) << endl;

	//min_rtt = numeric_limits<double>::max();

	unacknowledged_packets.clear();

	rtt_acked.reset();
	rtt_unacked.reset();
	prev_intersend_time = 0;

	intersend_time_vel = 0;
	prev_intersend_time_vel = 0;

	_intersend_time = 0;
	_the_window = num_probe_pkts;
	_timeout = 1000;
	
	monitor_interval_start = 0.0;
	num_losses = 0;
	prev_num_losses = 0;
	interarrival_time.reset();
	prev_ack_time = 0.0;
	pseudo_delay = 0.0;
	prev_pseudo_delay = 0.0;

  slow_start = true;

  prev_delta_update_time = 0.0;
  prev_delta_update_time_loss = 0.0;
  // percentile_delay = 0.0;
  loss_rate.reset();
  loss_in_last_rtt = 0.0;

	num_pkts_acked = num_pkts_lost = 0;
	flow_length = 0;

	#ifdef SIMULATION_MODE
	cur_tick = 0;
	#else
	start_time_point = std::chrono::high_resolution_clock::now();
  #endif
}

void MarkovianCC::update_delta(bool pkt_lost) {
	double cur_time = current_timestamp();
	double rtt_ewma = max((double)rtt_acked, (double)rtt_unacked);
	if (utility_mode == PFABRIC_FCT) {
		delta = 0.5 + 1.0 * (1.0 - exp(-1.0 * 1e-2 * flow_length));
		return;
  }
  // if (prev_delta_update_time_loss < cur_time - rtt_ewma) {
  //   cout << min_rtt << " " << cur_intersend_time << endl;
  //   prev_delta_update_time_loss = cur_time;
  // }
	
	if (utility_mode == BOUNDED_QDELAY_END)
		rtt_ewma -= min_rtt;

  if (utility_mode == BOUNDED_PERCENTILE_DELAY_END)
    rtt_ewma = percentile_delay.get_percentile_value();

	if (utility_mode == BOUNDED_DELAY_END || utility_mode == BOUNDED_QDELAY_END || utility_mode == BOUNDED_PERCENTILE_DELAY_END) {
		if (pkt_lost && prev_delta_update_time_loss < cur_time - rtt_ewma) {
      //delta += 0.1 * sqrt(delta);
      delta *= 2;
      //delta = min(delta, 1.0);
      slow_start = false;
      percentile_delay.reset();
			prev_delta_update_time_loss = cur_time;
			cout << delta << " " << rtt_ewma << " " << delay_bound << " " << pkt_lost << " " << cur_time << endl;
			return;
    }
		if (prev_delta_update_time > cur_time - rtt_ewma)
			return;
		prev_delta_update_time = cur_time;
		
		if (rtt_ewma > delay_bound) {
      //delta += 0.01 * sqrt(delta);
      //delta *= 1.33;
      delta *= 2;
      //delta = min(delta, 1.0);
      slow_start = false;
      percentile_delay.reset();
    }
		else if (rtt_ewma < delay_bound) {
      if (slow_start)
        delta /= 2;
      else
        if (delta < 1)
          delta = 1 / (1 / delta + 1);
        else
          delta -= 0.5;
    }
			//delta /= 1.1;
		//delta = max(0.01, delta);
		cout << "@ " << cur_time << " " << delta << " " << " " << rtt_ewma << " " << delay_bound << endl;
	}

	else if (utility_mode == MAX_THROUGHPUT) {
    assert(false);
	}
	else if (utility_mode == TCP_COOP) {
    // if (pkt_lost)
    //   loss_in_last_rtt = true;
		// if (prev_delta_update_time < cur_time - rtt_acked) {
    //   // Update loss_rate
    //   loss_rate *= 1.0 - alpha_loss * cur_intersend_time / rtt_ewma;
    //   if (loss_in_last_rtt) {
    //     loss_rate += alpha_loss * cur_intersend_time / rtt_ewma;
    //     loss_in_last_rtt = false;
    //   }
      
		// 	// if (loss_rate > 3  * cur_intersend_time * cur_intersend_time / (2.0 * rtt_ewma * rtt_ewma))
    //   if (loss_rate > 2.0 * cur_intersend_time / rtt_ewma) {
    //     if (delta < 0.1)
    //       delta += 0.01;
    //     else
    //       delta += 0.1;
    //     //delta += 0.1 * sqrt(delta);
    //   }
		// 	else
		// 		delta /= 1.1;
    //   prev_delta_update_time = cur_time;
		// 	cout << " " << delta << " " << cur_time << " " << cur_intersend_time << loss_rate << " " << 3 * cur_intersend_time / (2.0 * rtt_ewma) << endl;
		// }

    loss_rate.update(pkt_lost);
		if (prev_delta_update_time < cur_time - rtt_acked) {
      double loss = loss_rate.value();
      double allowed_rate = 1.0 / (rtt_ewma * sqrt(2.0 * loss / 3.0) + 4.0 * rtt_ewma * min(1.0 ,3.0 * sqrt(3.0 * loss / 8.0)) * loss * (1.0 + 32 * loss * loss));
      //double allowed_rate = 1.0 / (rtt_ewma * sqrt(loss));
      if (cur_intersend_time < 1e-40 || loss == 0.0) return;
			if (1.0 / cur_intersend_time > allowed_rate)
				delta += 0.1 * sqrt(delta); //delta += 0.01;
			else
				delta /= 1.1;
			prev_delta_update_time = cur_time;
			cout << " " << delta << " " << cur_time << " " << loss << " " << allowed_rate << " " << 1.0 / cur_intersend_time << endl;
    }
	}
	//assert(utility_mode == PFABRIC_FCT || utility_mode == CONSTANT_DELTA || utility_mode == BOUNDED_DELAY_END);
}

double MarkovianCC::randomize_intersend(double intersend) {
	if (intersend == 0)
		return 0;
	return rand_gen.exponential(intersend);
	//return rand_gen.uniform(0.5*intersend, 1.5*intersend);
	//return intersend;
}

void MarkovianCC::update_intersend_time() { 
	double cur_time __attribute((unused)) = current_timestamp();
	if (num_pkts_acked < num_probe_pkts - 1)
		return;
	_the_window = numeric_limits<double>::max();

	double queuing_delay = max((double)rtt_acked, (double)rtt_unacked) - min_rtt;
	double target_intersend_time = delta * sqrt(queuing_delay);

  // For second RTT, keep delta = 1
  // if (cur_time < 2 * rtt_acked) {
  //   target_intersend_time = max(1.0, delta) * queuing_delay;
  // }
  
	if (prev_intersend_time != 0)	
		cur_intersend_time = max(0.5 * prev_intersend_time, target_intersend_time);
	else
		cur_intersend_time = target_intersend_time;
  cur_intersend_time = max(target_intersend_time, 0.05);
	//cur_intersend_time = min(cur_intersend_time, min_rtt / 2.0);

	_intersend_time = randomize_intersend(cur_intersend_time);
	//cout << "@ " << cur_intersend_time << " " << queuing_delay << " " << _intersend_time << " " << rtt_acked << " " << rtt_unacked << endl;
}

void MarkovianCC::onACK(int ack, 
												double receiver_timestamp __attribute((unused)),
												double sent_time) {
	int seq_num = ack - 1;
	double cur_time = current_timestamp();

	assert(cur_time > sent_time);

	rtt_acked.update(cur_time - sent_time, cur_time / min_rtt);
	min_rtt = min(min_rtt, cur_time - sent_time);
	if (rtt_acked < min_rtt) 
		cout << "Warning: RTT < min_rtt" << endl;

	// Update interarrival time
	if (prev_ack_time != 0.0)
		interarrival_time.update(cur_time - prev_ack_time, cur_time / min_rtt);
	pseudo_delay *= pow(delta_decay_rate, (cur_time - prev_ack_time) / min_rtt);
	prev_ack_time = cur_time;
	if (cur_time - monitor_interval_start > max(rtt_acked, rtt_unacked)) {
		monitor_interval_start = cur_time;
		// if (num_losses >= 3)
		// 	cout << "High loss RTT" << endl;
		// else
		// 	cout << "Low loss RTT" << endl;
		//	cout << "Low loss RTT" << endl;
		if (prev_pseudo_delay != 0)
			prev_pseudo_delay = pseudo_delay;
		else
			prev_pseudo_delay = rtt_acked - min_rtt;
		prev_num_losses = num_losses;
		num_losses = 0;
	}

	percentile_delay.push(cur_time - sent_time);
	update_delta(false);
	update_intersend_time();

	if (unacknowledged_packets.count(seq_num) != 0 &&
			unacknowledged_packets[seq_num].sent_time == sent_time) {
		
		int tmp_seq_num = -1;
		for (auto x : unacknowledged_packets) {		
			assert(tmp_seq_num <= x.first);
			tmp_seq_num = x.first;
			if (x.first > seq_num)
				break;
			prev_intersend_time = x.second.intersend_time;
			prev_intersend_time_vel = x.second.intersend_time_vel;
			if (x.first < seq_num) {
				update_delta(true);
				++ num_losses;
				++ num_pkts_lost;
				pseudo_delay += interarrival_time;
				if (prev_pseudo_delay != 0.0)
					pseudo_delay = min(pseudo_delay, 2 * prev_pseudo_delay);
			}
			unacknowledged_packets.erase(x.first);
		}
	}

	++ num_pkts_acked;
}

void MarkovianCC::onPktSent(int seq_num) {
	double cur_time = current_timestamp();
	unacknowledged_packets[seq_num] = {cur_time, cur_intersend_time, intersend_time_vel};

	rtt_unacked.force_set(rtt_acked, cur_time / min_rtt);
	for (auto & x : unacknowledged_packets) {
		if (cur_time - x.second.sent_time > rtt_unacked) {
			rtt_unacked.update(cur_time - x.second.sent_time, cur_time / min_rtt);
			prev_intersend_time = x.second.intersend_time;
			prev_intersend_time_vel = x.second.intersend_time_vel;
			continue;
		}
		break;
	}
	//cout << "@ " << cur_time << " " << cur_intersend_time << " " << rtt_acked << " " << rtt_unacked << endl;

	_intersend_time = randomize_intersend(cur_intersend_time);
}

void MarkovianCC::close() {
}

void MarkovianCC::interpret_config_str(string config) {
	// Overriding config string for diff-delays experiment
	// utility_mode = BOUNDED_QDELAY_END;
	// delay_bound = 0.1;
	// cout << "Set delay bound to: " << delay_bound << endl;
	// return;

	// Format - delta_update_type:param1:param2...
	// Delta update types:
	//   -- constant_delta - params:- delta value
	//   -- pfabric_fct - params:- none
	//   -- bounded_delay - params:- delay bound (s)
  //   -- bounded_delay_end - params:- delay bound (s), done in an end-to-end manner
	delta = 1.0; // If delta is not set in time, we don't want it to be 0
	if (config.substr(0, 15) == "constant_delta:") {
		utility_mode = CONSTANT_DELTA;
		delta = atof(config.substr(15, string::npos).c_str());
		cout << "Constant delta mode with delta = " << delta << endl;
	}
	else if (config.substr(0, 11) == "pfabric_fct") {
		utility_mode = PFABRIC_FCT;
		cout << "Minimizing FCT PFabric style" << endl;
	}
	else if (config.substr(0, 14) == "bounded_delay:") {
		utility_mode = BOUNDED_DELAY;
		delay_bound = stof(config.substr(14, string::npos).c_str());
		cout << "Bounding delay to " << delay_bound << " s" << endl;
	}
	else if (config.substr(0, 18) == "bounded_delay_end:") {
		utility_mode = BOUNDED_DELAY_END;
		delay_bound = stof(config.substr(18, string::npos).c_str());
		cout << "Bounding delay to " << delay_bound << " s in an end-to-end manner" << endl;
	}
	else if (config.substr(0, 18) == "bounded_qdelay_end:") {
		utility_mode = BOUNDED_QDELAY_END;
		delay_bound = stof(config.substr(18, string::npos).c_str());
		cout << "Bounding queuing delay to " << delay_bound << " s in an end-to-end manner" << endl;
	}
  else if (config.substr(0, 29) == "bounded_percentile_delay_end:") {
    utility_mode = BOUNDED_PERCENTILE_DELAY_END;
    delay_bound = stof(config.substr(29, string::npos).c_str());
		cout << "Bounding percentile delay to " << delay_bound << " s in an end-to-end manner" << endl;
  }
	else if (config.substr(0, 18) == "bounded_fdelay_end:") {
		utility_mode = BOUNDED_FDELAY_END;
		delay_bound = stof(config.substr(18, string::npos).c_str());
		cout << "Bounding fractional queuing delay to " << delay_bound << " s in an end-to-end manner" << endl;
	}
	else if (config.substr(0, 14) == "max_throughput") {
		utility_mode = MAX_THROUGHPUT;
		cout << "Maximizing throughput" << endl;
	}
	else if (config.substr(0, 16) == "different_deltas") {
    assert(false);
		utility_mode = CONSTANT_DELTA;
		// delta = flow_id * 0.5;
		cout << "Setting constant delta to " << delta << endl;
	}
	else if (config.substr(0, 8) == "tcp_coop") {
		utility_mode = TCP_COOP;
		cout << "Cooperating with TCP" << delta << endl;
	}
	else {
		utility_mode = CONSTANT_DELTA;
		delta = 1.0;
		cout << "Incorrect format of configuration string '" << config
				 << "'. Using constant delta mode with delta = 1 by default" << endl;
	}
}
